/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "space.h"

#include "activation.h"
#include "active_window.h"
#include "deco/bridge.h"
#include "desktop_space.h"
#include "output_space.h"
#include "session.h"
#include "singleton_interface.h"
#include "space_areas_helpers.h"
#include "window_area.h"
#include "x11/tool_windows.h"

#include "base/dbus/kwin.h"
#include "base/output_helpers.h"
#include "base/x11/user_interaction_filter.h"
#include "base/x11/xcb/extensions.h"
#include "input/cursor.h"
#include "main.h"
#include "render/effects.h"
#include "render/outline.h"
#include "render/platform.h"
#include "render/post/night_color_manager.h"
#include "rules/rule_book.h"
#include "rules/rules.h"
#include "scripting/platform.h"
#include "utils/blocker.h"
#include "win/controlling.h"
#include "win/dbus/appmenu.h"
#include "win/dbus/virtual_desktop_manager.h"
#include "win/input.h"
#include "win/internal_window.h"
#include "win/kill_window.h"
#include "win/layers.h"
#include "win/remnant.h"
#include "win/screen_edges.h"
#include "win/setup.h"
#include "win/shortcut_dialog.h"
#include "win/stacking.h"
#include "win/stacking_order.h"
#include "win/user_actions_menu.h"
#include "win/util.h"
#include "win/virtual_desktops.h"
#include "win/x11/control.h"
#include "win/x11/event.h"
#include "win/x11/group.h"
#include "win/x11/moving_window_filter.h"
#include "win/x11/netinfo.h"
#include "win/x11/space_areas.h"
#include "win/x11/space_setup.h"
#include "win/x11/stacking.h"
#include "win/x11/sync_alarm_filter.h"
#include "win/x11/transient.h"
#include "win/x11/unmanaged.h"
#include "win/x11/window.h"

#if KWIN_BUILD_TABBOX
#include "tabbox/tabbox.h"
#endif

// TODO(romangg): For now this needs to be included late because of some conflict with Qt libraries.
#include "space_reconfigure.h"

#include <KStartupInfo>
#include <QAction>
#include <QtConcurrentRun>
#include <cassert>
#include <memory>

namespace KWin::win
{

space_qobject::space_qobject(std::function<void()> reconfigure_callback)
    : reconfigure_callback{reconfigure_callback}
{
}

void space_qobject::reconfigure()
{
    reconfigure_callback();
}

space::space(render::compositor& render)
    : qobject{std::make_unique<space_qobject>([this] { space_start_reconfigure_timer(*this); })}
    , outline{std::make_unique<render::outline>(render)}
    , render{render}
    , deco{std::make_unique<deco::bridge<space>>(*this)}
    , appmenu{std::make_unique<dbus::appmenu>(dbus::create_appmenu_callbacks(*this))}
    , rule_book{std::make_unique<RuleBook>()}
    , user_actions_menu{std::make_unique<win::user_actions_menu<space>>(*this)}
    , stacking_order{std::make_unique<win::stacking_order>()}
    , focus_chain{win::focus_chain<space>(*this)}
    , virtual_desktop_manager{std::make_unique<win::virtual_desktop_manager>()}
    , dbus{std::make_unique<base::dbus::kwin_impl<space>>(*this)}
    , session_manager{std::make_unique<win::session_manager>()}
{
    // For invoke methods of user_actions_menu.
    qRegisterMetaType<Toplevel*>();

    singleton_interface::space = this;

    m_quickTileCombineTimer = new QTimer(qobject.get());
    m_quickTileCombineTimer->setSingleShot(true);

    init_rule_book(*rule_book, *this);

    // dbus interface
    new win::dbus::virtual_desktop_manager(virtual_desktop_manager.get());

#if KWIN_BUILD_TABBOX
    // need to create the tabbox before compositing scene is setup
    tabbox = std::make_unique<win::tabbox>(*this);
#endif

    QObject::connect(qobject.get(),
                     &space_qobject::currentDesktopChanged,
                     &render,
                     &render::compositor::addRepaintFull);

    deco->init();
    QObject::connect(qobject.get(), &space_qobject::configChanged, deco->qobject.get(), [this] {
        deco->reconfigure();
    });

    QObject::connect(session_manager.get(),
                     &win::session_manager::loadSessionRequested,
                     qobject.get(),
                     [this](auto&& session_name) { load_session_info(*this, session_name); });
    QObject::connect(
        session_manager.get(),
        &win::session_manager::prepareSessionSaveRequested,
        qobject.get(),
        [this](const QString& name) { store_session(*this, name, win::sm_save_phase0); });
    QObject::connect(
        session_manager.get(),
        &win::session_manager::finishSessionSaveRequested,
        qobject.get(),
        [this](const QString& name) { store_session(*this, name, win::sm_save_phase2); });

    auto& base = kwinApp()->get_base();
    QObject::connect(
        &base, &base::platform::topology_changed, qobject.get(), [this](auto old, auto topo) {
            if (old.size != topo.size) {
                handle_desktop_resize(*this);
            }
        });

    QObject::connect(qobject.get(), &qobject_t::clientRemoved, qobject.get(), [this](auto window) {
        focus_chain_remove(focus_chain, window);
    });
    QObject::connect(qobject.get(),
                     &qobject_t::clientActivated,
                     qobject.get(),
                     [this](auto window) { focus_chain.active_window = window; });
    QObject::connect(virtual_desktop_manager.get(),
                     &win::virtual_desktop_manager::countChanged,
                     qobject.get(),
                     [this](auto prev, auto next) { focus_chain_resize(focus_chain, prev, next); });
    QObject::connect(virtual_desktop_manager.get(),
                     &win::virtual_desktop_manager::currentChanged,
                     qobject.get(),
                     [this](auto /*prev*/, auto next) { focus_chain.current_desktop = next; });
    QObject::connect(kwinApp()->options.get(),
                     &base::options::separateScreenFocusChanged,
                     qobject.get(),
                     [this](auto enable) { focus_chain.has_separate_screen_focus = enable; });
    focus_chain.has_separate_screen_focus = kwinApp()->options->isSeparateScreenFocus();

    auto vds = virtual_desktop_manager.get();
    QObject::connect(
        vds,
        &win::virtual_desktop_manager::countChanged,
        qobject.get(),
        [this](auto prev, auto next) { handle_desktop_count_changed(*this, prev, next); });
    QObject::connect(
        vds,
        &win::virtual_desktop_manager::currentChanged,
        qobject.get(),
        [this](auto prev, auto next) { handle_current_desktop_changed(*this, prev, next); });
    vds->setNavigationWrappingAround(kwinApp()->options->isRollOverDesktops());
    QObject::connect(kwinApp()->options.get(),
                     &base::options::rollOverDesktopsChanged,
                     vds,
                     &win::virtual_desktop_manager::setNavigationWrappingAround);

    auto config = kwinApp()->config();
    vds->setConfig(config);

    // positioning object needs to be created before the virtual desktops are loaded.
    vds->load();
    vds->updateLayout();

    // makes sure any autogenerated id is saved, necessary as in case of xwayland, load will be
    // called 2 times
    // load is needed to be called again when starting xwayalnd to sync to RootInfo, see BUG 385260
    vds->save();

    if (!vds->setCurrent(m_initialDesktop)) {
        vds->setCurrent(1);
    }

    reconfigureTimer.setSingleShot(true);
    updateToolWindowsTimer.setSingleShot(true);

    QObject::connect(
        &reconfigureTimer, &QTimer::timeout, qobject.get(), [this] { space_reconfigure(*this); });
    QObject::connect(&updateToolWindowsTimer, &QTimer::timeout, qobject.get(), [this] {
        x11::update_tool_windows_visibility(this, true);
    });

    // TODO: do we really need to reconfigure everything when fonts change?
    // maybe just reconfigure the decorations? Move this into libkdecoration?
    QDBusConnection::sessionBus().connect(QString(),
                                          QStringLiteral("/KDEPlatformTheme"),
                                          QStringLiteral("org.kde.KDEPlatformTheme"),
                                          QStringLiteral("refreshFonts"),
                                          qobject.get(),
                                          SLOT(reconfigure()));

    active_client = nullptr;
    QObject::connect(
        stacking_order.get(), &stacking_order::changed, qobject.get(), [this](auto count_changed) {
            x11::propagate_clients(*this, count_changed);
            if (active_client) {
                active_client->control->update_mouse_grab();
            }
        });
    QObject::connect(stacking_order.get(), &stacking_order::render_restack, qobject.get(), [this] {
        x11::render_stack_unmanaged_windows(*this);
    });
}

space::~space()
{
    stacking_order->lock();

    // TODO: grabXServer();

    win::x11::clear_space(*this);

    for (auto const& window : windows) {
        if (auto internal = qobject_cast<win::internal_window*>(window);
            internal && !internal->remnant) {
            internal->destroyClient();
            remove_all(windows, internal);
        }
    }

    // At this point only remnants are remaining.
    for (auto it = windows.begin(); it != windows.end();) {
        assert((*it)->remnant);
        Q_EMIT qobject->window_deleted(*it);
        it = windows.erase(it);
    }

    assert(windows.empty());

    stacking_order.reset();

    rule_book.reset();
    kwinApp()->config()->sync();

    win::x11::root_info::destroy();
    delete startup;
    delete client_keys_dialog;
    for (auto const& s : session)
        delete s;

    // TODO: ungrabXServer();

    base::x11::xcb::extensions::destroy();
    singleton_interface::space = nullptr;
}

win::screen_edge* space::create_screen_edge(win::screen_edger& edger)
{
    return new win::screen_edge(&edger);
}

QRect space::get_icon_geometry(Toplevel const* /*win*/) const
{
    return QRect();
}

void space::update_space_area_from_windows(QRect const& /*desktop_area*/,
                                           std::vector<QRect> const& /*screens_geos*/,
                                           win::space_areas& /*areas*/)
{
    // Can't be pure virtual because the function might be called from the ctor.
}

}
