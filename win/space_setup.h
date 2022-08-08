/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <config-kwin.h>

#include "dbus/virtual_desktop_manager.h"
#include "internal_window.h"
#include "rules.h"
#include "x11/space_setup.h"
#include "x11/stacking.h"

#include "base/platform.h"
#include "toplevel.h"

#include <QObject>

namespace KWin::win
{

template<typename Space>
void init_space(Space& space)
{
    // For invoke methods of user_actions_menu.
    qRegisterMetaType<Toplevel*>();

    space.m_quickTileCombineTimer = new QTimer(space.qobject.get());
    space.m_quickTileCombineTimer->setSingleShot(true);

    init_rule_book(*space.rule_book, space);

    // dbus interface
    new dbus::virtual_desktop_manager(space.virtual_desktop_manager.get());

#if KWIN_BUILD_TABBOX
    // need to create the tabbox before compositing scene is setup
    space.tabbox = std::make_unique<win::tabbox>(space);
#endif

    QObject::connect(space.qobject.get(),
                     &Space::qobject_t::currentDesktopChanged,
                     space.render.qobject.get(),
                     [comp = &space.render] { comp->addRepaintFull(); });

    space.deco->init();
    QObject::connect(space.qobject.get(),
                     &Space::qobject_t::configChanged,
                     space.deco->qobject.get(),
                     [&] { space.deco->reconfigure(); });

    QObject::connect(space.session_manager.get(),
                     &session_manager::loadSessionRequested,
                     space.qobject.get(),
                     [&](auto&& session_name) { load_session_info(space, session_name); });
    QObject::connect(space.session_manager.get(),
                     &session_manager::prepareSessionSaveRequested,
                     space.qobject.get(),
                     [&](auto const& name) { store_session(space, name, sm_save_phase0); });
    QObject::connect(space.session_manager.get(),
                     &session_manager::finishSessionSaveRequested,
                     space.qobject.get(),
                     [&](auto const& name) { store_session(space, name, sm_save_phase2); });

    auto& base = kwinApp()->get_base();
    QObject::connect(
        &base, &base::platform::topology_changed, space.qobject.get(), [&](auto old, auto topo) {
            if (old.size != topo.size) {
                space.resize(topo.size);
            }
        });

    QObject::connect(space.qobject.get(),
                     &Space::qobject_t::clientRemoved,
                     space.qobject.get(),
                     [&](auto window) { focus_chain_remove(space.focus_chain, window); });
    QObject::connect(space.qobject.get(),
                     &Space::qobject_t::clientActivated,
                     space.qobject.get(),
                     [&](auto window) { space.focus_chain.active_window = window; });
    QObject::connect(
        space.virtual_desktop_manager->qobject.get(),
        &virtual_desktop_manager_qobject::countChanged,
        space.qobject.get(),
        [&](auto prev, auto next) { focus_chain_resize(space.focus_chain, prev, next); });
    QObject::connect(space.virtual_desktop_manager->qobject.get(),
                     &win::virtual_desktop_manager_qobject::currentChanged,
                     space.qobject.get(),
                     [&](auto /*prev*/, auto next) { space.focus_chain.current_desktop = next; });
    QObject::connect(kwinApp()->options->qobject.get(),
                     &base::options_qobject::separateScreenFocusChanged,
                     space.qobject.get(),
                     [&](auto enable) { space.focus_chain.has_separate_screen_focus = enable; });
    space.focus_chain.has_separate_screen_focus
        = kwinApp()->options->qobject->isSeparateScreenFocus();

    auto& vds = space.virtual_desktop_manager;
    QObject::connect(
        vds->qobject.get(),
        &win::virtual_desktop_manager_qobject::countChanged,
        space.qobject.get(),
        [&](auto prev, auto next) { handle_desktop_count_changed(space, prev, next); });

    QObject::connect(vds->qobject.get(),
                     &win::virtual_desktop_manager_qobject::currentChanged,
                     space.qobject.get(),
                     [&](auto prev, auto next) {
                         close_active_popup(space);

                         blocker block(space.stacking_order);
                         update_client_visibility_on_desktop_change(&space, next);

                         if (space.showing_desktop) {
                             // Do this only after desktop change to avoid flicker.
                             set_showing_desktop(space, false);
                         }

                         activate_window_on_new_desktop(space, next);
                         Q_EMIT space.qobject->currentDesktopChanged(prev,
                                                                     space.move_resize_window);
                     });

    vds->setNavigationWrappingAround(kwinApp()->options->qobject->isRollOverDesktops());
    QObject::connect(kwinApp()->options->qobject.get(),
                     &base::options_qobject::rollOverDesktopsChanged,
                     vds->qobject.get(),
                     [&vds](auto enabled) { vds->setNavigationWrappingAround(enabled); });

    auto config = kwinApp()->config();
    vds->setConfig(config);

    // positioning object needs to be created before the virtual desktops are loaded.
    vds->load();
    vds->updateLayout();

    // makes sure any autogenerated id is saved, necessary as in case of xwayland, load will be
    // called 2 times
    // load is needed to be called again when starting xwayalnd to sync to RootInfo, see BUG 385260
    vds->save();

    if (!vds->setCurrent(space.m_initialDesktop)) {
        vds->setCurrent(1);
    }

    space.reconfigureTimer.setSingleShot(true);
    space.updateToolWindowsTimer.setSingleShot(true);

    QObject::connect(&space.reconfigureTimer, &QTimer::timeout, space.qobject.get(), [&] {
        space_reconfigure(space);
    });
    QObject::connect(&space.updateToolWindowsTimer, &QTimer::timeout, space.qobject.get(), [&] {
        x11::update_tool_windows_visibility(&space, true);
    });

    // TODO: do we really need to reconfigure everything when fonts change?
    // maybe just reconfigure the decorations? Move this into libkdecoration?
    QDBusConnection::sessionBus().connect(QString(),
                                          QStringLiteral("/KDEPlatformTheme"),
                                          QStringLiteral("org.kde.KDEPlatformTheme"),
                                          QStringLiteral("refreshFonts"),
                                          space.qobject.get(),
                                          SLOT(reconfigure()));

    space.active_client = nullptr;
    QObject::connect(space.stacking_order.get(),
                     &stacking_order::changed,
                     space.qobject.get(),
                     [&](auto count_changed) {
                         x11::propagate_clients(space, count_changed);
                         if (space.active_client) {
                             space.active_client->control->update_mouse_grab();
                         }
                     });
    QObject::connect(space.stacking_order.get(),
                     &stacking_order::render_restack,
                     space.qobject.get(),
                     [&] { x11::render_stack_unmanaged_windows(space); });
}

template<typename Space>
void clear_space(Space& space)
{
    space.stacking_order->lock();

    // TODO: grabXServer();

    x11::clear_space(space);

    for (auto const& window : space.windows) {
        if (auto internal = qobject_cast<internal_window*>(window);
            internal && !internal->remnant) {
            internal->destroyClient();
            remove_all(space.windows, internal);
        }
    }

    // At this point only remnants are remaining.
    for (auto it = space.windows.begin(); it != space.windows.end();) {
        assert((*it)->remnant);
        Q_EMIT space.qobject->window_deleted(*it);
        it = space.windows.erase(it);
    }

    assert(space.windows.empty());

    space.stacking_order.reset();

    space.rule_book.reset();
    kwinApp()->config()->sync();

    x11::root_info::destroy();
    delete space.startup;
    delete space.client_keys_dialog;
    for (auto const& s : space.session)
        delete s;

    // TODO: ungrabXServer();

    base::x11::xcb::extensions::destroy();
}

}
