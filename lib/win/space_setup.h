/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <config-kwin.h>

#include "dbus/virtual_desktop_manager.h"
#include "rules.h"
#include "tabbox/tabbox.h"
#include <base/platform_qobject.h>
#include <win/options.h>
#include <win/space_reconfigure.h>

#include <KLocalizedContext>
#include <QObject>
#include <QQmlEngine>
#include <QQuickWindow>

namespace KWin::win
{

template<typename Space>
void init_space(Space& space)
{
    // We want all QQuickWindows with an alpha buffer.
    QQuickWindow::setDefaultAlphaBuffer(true);

    space.qml_engine = std::make_unique<QQmlEngine>();
    space.qml_engine->setProperty("_kirigamiTheme", QStringLiteral("KirigamiPlasmaStyle"));
    space.qml_engine->rootContext()->setContextObject(
        new KLocalizedContext(space.qml_engine.get()));

    space.m_quickTileCombineTimer = new QTimer(space.qobject.get());
    space.m_quickTileCombineTimer->setSingleShot(true);

    init_rule_book(*space.rule_book, space);

    // dbus interface
    new dbus::subspace_manager(*space.subspace_manager);

#if KWIN_BUILD_TABBOX
    // need to create the tabbox before compositing scene is setup
    space.tabbox = std::make_unique<win::tabbox<Space>>(space);
#endif

    space.deco->init();
    QObject::connect(space.qobject.get(),
                     &Space::qobject_t::configChanged,
                     space.deco->qobject.get(),
                     [&] { space.deco->reconfigure(); });

    QObject::connect(space.base.qobject.get(),
                     &base::platform_qobject::topology_changed,
                     space.qobject.get(),
                     [&](auto old, auto topo) {
                         if (old.size != topo.size) {
                             space.resize(topo.size);
                         }
                     });

    QObject::connect(space.qobject.get(),
                     &Space::qobject_t::clientRemoved,
                     space.qobject.get(),
                     [&](auto win_id) {
                         auto window = space.windows_map.at(win_id);
                         std::visit(overload{[&space](auto&& win) {
                                        focus_chain_remove(space.stacking.focus_chain, win);
                                    }},
                                    window);
                     });
    QObject::connect(space.qobject.get(),
                     &Space::qobject_t::clientActivated,
                     space.qobject.get(),
                     [&] { space.stacking.focus_chain.active_window = space.stacking.active; });

    using subspace_manager_qobject_t = decltype(space.subspace_manager->qobject)::element_type;
    QObject::connect(
        space.subspace_manager->qobject.get(),
        &subspace_manager_qobject_t::countChanged,
        space.qobject.get(),
        [&](auto prev, auto next) { focus_chain_resize(space.stacking.focus_chain, prev, next); });
    QObject::connect(space.subspace_manager->qobject.get(),
                     &subspace_manager_qobject_t::current_changed,
                     space.qobject.get(),
                     [&](auto /*prev*/, auto next) {
                         space.stacking.focus_chain.current_subspace = next->x11DesktopNumber();
                     });
    QObject::connect(
        space.options->qobject.get(),
        &options_qobject::separateScreenFocusChanged,
        space.qobject.get(),
        [&](auto enable) { space.stacking.focus_chain.has_separate_screen_focus = enable; });
    space.stacking.focus_chain.has_separate_screen_focus
        = space.options->qobject->isSeparateScreenFocus();

    auto& subs_manager = space.subspace_manager;
    QObject::connect(
        subs_manager->qobject.get(),
        &subspace_manager_qobject_t::countChanged,
        space.qobject.get(),
        [&](auto prev, auto next) { handle_subspace_count_changed(space, prev, next); });

    QObject::connect(subs_manager->qobject.get(),
                     &subspace_manager_qobject_t::current_changed,
                     space.qobject.get(),
                     [&](auto prev, auto next) {
                         close_active_popup(space);

                         blocker block(space.stacking.order);
                         update_client_visibility_on_subspace_change(&space,
                                                                     next->x11DesktopNumber());

                         if (space.showing_desktop) {
                             // Do this only after subspace change to avoid flicker.
                             set_showing_desktop(space, false);
                         }

                         activate_window_on_new_subspace(space, next->x11DesktopNumber());
                         Q_EMIT space.qobject->current_subspace_changed(prev);
                     });

    QObject::connect(subs_manager->qobject.get(),
                     &subspace_manager_qobject_t::current_changing,
                     space.qobject.get(),
                     [&](auto current_subspace, auto offset) {
                         close_active_popup(space);
                         Q_EMIT space.qobject->current_subspace_changing(current_subspace, offset);
                     });
    QObject::connect(subs_manager->qobject.get(),
                     &subspace_manager_qobject_t::current_changing_cancelled,
                     space.qobject.get(),
                     [&]() { Q_EMIT space.qobject->current_subspace_changing_cancelled(); });

    subspace_manager_set_nav_wraps(*subs_manager, space.options->qobject->isRollOverDesktops());
    QObject::connect(
        space.options->qobject.get(),
        &options_qobject::rollOverDesktopsChanged,
        subs_manager->qobject.get(),
        [&subs_manager](auto enabled) { subspace_manager_set_nav_wraps(*subs_manager, enabled); });

    subs_manager->config = space.base.config.main;

    // positioning object needs to be created before the virtual subspaces are loaded.
    subspace_manager_load(*subs_manager);
    subspace_manager_update_layout(*subs_manager);

    // makes sure any autogenerated id is saved, necessary as in case of xwayland, load will be
    // called 2 times
    // load is needed to be called again when starting xwayalnd to sync to RootInfo, see BUG 385260
    subspace_manager_save(*subs_manager);

    if (!subspaces_set_current(*subs_manager, space.initial_subspace)) {
        subspaces_set_current(*subs_manager, 1);
    }

    space.reconfigureTimer.setSingleShot(true);
    space.updateToolWindowsTimer.setSingleShot(true);

    QObject::connect(&space.reconfigureTimer, &QTimer::timeout, space.qobject.get(), [&] {
        space_reconfigure(space);
    });

    // TODO: do we really need to reconfigure everything when fonts change?
    // maybe just reconfigure the decorations? Move this into libkdecoration?
    QDBusConnection::sessionBus().connect(QString(),
                                          QStringLiteral("/KDEPlatformTheme"),
                                          QStringLiteral("org.kde.KDEPlatformTheme"),
                                          QStringLiteral("refreshFonts"),
                                          space.qobject.get(),
                                          SLOT(reconfigure()));

    space.stacking.active = {};
}

template<typename Space>
void clear_space(Space& space)
{
    using var_win = typename Space::window_t;

    space.stacking.order.lock();

    if constexpr (requires { typename Space::internal_window_t; }) {
        using int_win = typename Space::internal_window_t;
        auto const windows_copy = space.windows;
        for (auto const& window : windows_copy) {
            std::visit(overload{
                           [&](int_win* win) {
                               if (!win->remnant) {
                                   win->destroyClient();
                                   remove_all(space.windows, var_win(win));
                               }
                           },
                           [](auto&&) {},
                       },
                       window);
        }
    }

    // At this point only remnants are remaining.
    for (auto it = space.windows.begin(); it != space.windows.end();) {
        std::visit(overload{[&](auto&& win) {
                       assert(win->remnant);
                       Q_EMIT space.qobject->window_deleted(win->meta.signal_id);
                   }},
                   *it);
        it = space.windows.erase(it);
    }

    assert(space.windows.empty());

    space.rule_book.reset();
    space.base.config.main->sync();

    if constexpr (requires(Space space) { space.root_info; }) {
        space.root_info.reset();
        for (auto const& s : space.session) {
            delete s;
        }
    }

    delete space.client_keys_dialog;
    space.base.mod.render->space = nullptr;
}

}
