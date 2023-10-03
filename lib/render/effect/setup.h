/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <render/x11/effect.h>
#include <render/x11/property_notify_filter.h>
#include <win/screen_edges.h>
#include <win/session_manager.h>

#include <config-kwin.h>

#if KWIN_BUILD_TABBOX
#include <win/tabbox/tabbox.h>
#endif

#include <QObject>
#include <variant>

namespace KWin::effect
{

template<typename Handler, typename Win>
void setup_handler_window_connections(Handler& handler, Win& window)
{
    auto qtwin = window.qobject.get();

    QObject::connect(qtwin, &win::window_qobject::subspaces_changed, &handler, [&handler, &window] {
        Q_EMIT handler.windowDesktopsChanged(window.render->effect.get());
    });
    QObject::connect(qtwin,
                     &win::window_qobject::maximize_mode_changed,
                     &handler,
                     [&handler, &window](auto mode) { handler.slotClientMaximized(window, mode); });
    QObject::connect(
        qtwin, &win::window_qobject::clientStartUserMovedResized, &handler, [&handler, &window] {
            Q_EMIT handler.windowStartUserMovedResized(window.render->effect.get());
        });
    QObject::connect(qtwin,
                     &win::window_qobject::clientStepUserMovedResized,
                     &handler,
                     [&handler, &window](QRect const& geometry) {
                         Q_EMIT handler.windowStepUserMovedResized(window.render->effect.get(),
                                                                   geometry);
                     });
    QObject::connect(
        qtwin, &win::window_qobject::clientFinishUserMovedResized, &handler, [&handler, &window] {
            Q_EMIT handler.windowFinishUserMovedResized(window.render->effect.get());
        });
    QObject::connect(qtwin,
                     &win::window_qobject::opacityChanged,
                     &handler,
                     [&handler, &window](auto old) { handler.slotOpacityChanged(window, old); });
    QObject::connect(
        qtwin, &win::window_qobject::clientMinimized, &handler, [&handler, &window](auto animate) {
            // TODO: notify effects even if it should not animate?
            if (animate) {
                Q_EMIT handler.windowMinimized(window.render->effect.get());
            }
        });
    QObject::connect(qtwin,
                     &win::window_qobject::clientUnminimized,
                     &handler,
                     [&handler, &window](auto animate) {
                         // TODO: notify effects even if it should not animate?
                         if (animate) {
                             Q_EMIT handler.windowUnminimized(window.render->effect.get());
                         }
                     });
    QObject::connect(qtwin, &win::window_qobject::modalChanged, &handler, [&handler, &window] {
        handler.slotClientModalityChanged(window);
    });
    QObject::connect(
        qtwin,
        &win::window_qobject::frame_geometry_changed,
        &handler,
        [&handler, &window](auto const& rect) { handler.slotFrameGeometryChanged(window, rect); });
    QObject::connect(
        qtwin, &win::window_qobject::damaged, &handler, [&handler, &window](auto const& rect) {
            handler.slotWindowDamaged(window, rect);
        });
    QObject::connect(qtwin,
                     &win::window_qobject::unresponsiveChanged,
                     &handler,
                     [&handler, &window](bool unresponsive) {
                         Q_EMIT handler.windowUnresponsiveChanged(window.render->effect.get(),
                                                                  unresponsive);
                     });
    QObject::connect(qtwin, &win::window_qobject::windowShown, &handler, [&handler, &window] {
        Q_EMIT handler.windowShown(window.render->effect.get());
    });
    QObject::connect(qtwin, &win::window_qobject::windowHidden, &handler, [&handler, &window] {
        Q_EMIT handler.windowHidden(window.render->effect.get());
    });
    QObject::connect(
        qtwin, &win::window_qobject::keepAboveChanged, &handler, [&handler, &window](bool above) {
            Q_UNUSED(above)
            Q_EMIT handler.windowKeepAboveChanged(window.render->effect.get());
        });
    QObject::connect(
        qtwin, &win::window_qobject::keepBelowChanged, &handler, [&handler, &window](bool below) {
            Q_UNUSED(below)
            Q_EMIT handler.windowKeepBelowChanged(window.render->effect.get());
        });
    QObject::connect(
        qtwin, &win::window_qobject::fullScreenChanged, &handler, [&handler, &window]() {
            Q_EMIT handler.windowFullScreenChanged(window.render->effect.get());
        });
    QObject::connect(
        qtwin, &win::window_qobject::visible_geometry_changed, &handler, [&handler, &window]() {
            Q_EMIT handler.windowExpandedGeometryChanged(window.render->effect.get());
        });
}

template<typename Handler, typename Win>
void setup_handler_x11_controlled_window_connections(Handler& handler, Win& window)
{
    setup_handler_window_connections(handler, window);
    QObject::connect(
        window.qobject.get(),
        &win::window_qobject::paddingChanged,
        &handler,
        [&handler, &window](auto const& old) { handler.slotPaddingChanged(window, old); });
}

template<typename Handler, typename Win>
void setup_handler_x11_unmanaged_window_connections(Handler& handler, Win& window)
{
    QObject::connect(window.qobject.get(),
                     &win::window_qobject::opacityChanged,
                     &handler,
                     [&handler, &window](auto old) { handler.slotOpacityChanged(window, old); });
    QObject::connect(
        window.qobject.get(),
        &win::window_qobject::frame_geometry_changed,
        &handler,
        [&handler, &window](auto const& old) { handler.slotFrameGeometryChanged(window, old); });
    QObject::connect(
        window.qobject.get(),
        &win::window_qobject::paddingChanged,
        &handler,
        [&handler, &window](auto const& old) { handler.slotPaddingChanged(window, old); });
    QObject::connect(
        window.qobject.get(),
        &win::window_qobject::damaged,
        &handler,
        [&handler, &window](auto const& region) { handler.slotWindowDamaged(window, region); });
    QObject::connect(window.qobject.get(),
                     &win::window_qobject::visible_geometry_changed,
                     &handler,
                     [&handler, &window]() {
                         Q_EMIT handler.windowExpandedGeometryChanged(window.render->effect.get());
                     });
}

template<typename Handler>
void setup_handler(Handler& handler)
{
    QObject::connect(&handler, &Handler::hasActiveFullScreenEffectChanged, &handler, [&handler] {
        Q_EMIT handler.scene.compositor.platform.base.space->edges->qobject->checkBlocking();
    });

    auto ws = handler.scene.compositor.platform.base.space.get();
    auto& vds = ws->subspace_manager;

    QObject::connect(ws->qobject.get(),
                     &win::space_qobject::showingDesktopChanged,
                     &handler,
                     &Handler::showingDesktopChanged);
    QObject::connect(ws->qobject.get(),
                     &win::space_qobject::currentDesktopChanged,
                     &handler,
                     [&handler, space = ws](int old) {
                         int const newDesktop = space->subspace_manager->current();
                         if (old == 0 || newDesktop == old) {
                             return;
                         }
                         EffectWindow* eff_win{nullptr};
                         if (auto& mov_res = space->move_resize_window) {
                             std::visit(overload{[&](auto&& win) {
                                            assert(win->render);
                                            assert(win->render->effect);
                                            eff_win = win->render->effect.get();
                                        }},
                                        *mov_res);
                         }
                         Q_EMIT handler.desktopChanged(old, newDesktop, eff_win);
                     });
    QObject::connect(ws->qobject.get(),
                     &win::space_qobject::currentDesktopChanging,
                     &handler,
                     [&handler, space = ws](uint currentDesktop, QPointF offset) {
                         EffectWindow* eff_win{nullptr};
                         if (auto& mov_res = space->move_resize_window) {
                             std::visit(overload{[&](auto&& win) {
                                            assert(win->render);
                                            assert(win->render->effect);
                                            eff_win = win->render->effect.get();
                                        }},
                                        *mov_res);
                         }
                         Q_EMIT handler.desktopChanging(currentDesktop, offset, eff_win);
                     });
    QObject::connect(ws->qobject.get(),
                     &win::space_qobject::currentDesktopChangingCancelled,
                     &handler,
                     [&handler]() { Q_EMIT handler.desktopChangingCancelled(); });
    QObject::connect(ws->qobject.get(),
                     &win::space_qobject::clientAdded,
                     &handler,
                     [&handler, space = ws](auto win_id) {
                         std::visit(overload{[&handler](auto&& win) {
                                        if (win->render_data.ready_for_painting) {
                                            handler.slotClientShown(*win);
                                        } else {
                                            QObject::connect(
                                                win->qobject.get(),
                                                &win::window_qobject::windowShown,
                                                &handler,
                                                [&handler, win] { handler.slotClientShown(*win); });
                                        }
                                    }},
                                    space->windows_map.at(win_id));
                     });
    QObject::connect(ws->qobject.get(),
                     &win::space_qobject::unmanagedAdded,
                     &handler,
                     [&handler, space = ws](auto win_id) {
                         // it's never initially ready but has synthetic 50ms delay
                         std::visit(overload{[&handler](auto&& win) {
                                        QObject::connect(
                                            win->qobject.get(),
                                            &win::window_qobject::windowShown,
                                            &handler,
                                            [&handler, win] { handler.slotUnmanagedShown(*win); });
                                    }},
                                    space->windows_map.at(win_id));
                     });
    QObject::connect(ws->qobject.get(),
                     &win::space_qobject::internalClientAdded,
                     &handler,
                     [&handler, space = ws](auto win_id) {
                         std::visit(overload{[&handler](auto&& win) {
                                        assert(win->render);
                                        assert(win->render->effect);
                                        setup_handler_window_connections(handler, *win);
                                        Q_EMIT handler.windowAdded(win->render->effect.get());
                                    }},
                                    space->windows_map.at(win_id));
                     });
    QObject::connect(
        ws->qobject.get(), &win::space_qobject::clientActivated, &handler, [&handler, space = ws] {
            EffectWindow* eff_win{nullptr};
            if (auto win = space->stacking.active) {
                std::visit(overload{[&](auto&& win) {
                               assert(win->render);
                               assert(win->render->effect);
                               eff_win = win->render->effect.get();
                           }},
                           *win);
            }
            Q_EMIT handler.windowActivated(eff_win);
        });

    QObject::connect(ws->qobject.get(),
                     &win::space_qobject::window_deleted,
                     &handler,
                     [&handler, space = ws](auto win_id) {
                         std::visit(overload{[&handler](auto&& win) {
                                        assert(win->render);
                                        assert(win->render->effect);
                                        Q_EMIT handler.windowDeleted(win->render->effect.get());
                                        handler.elevated_windows.removeAll(
                                            win->render->effect.get());
                                    }},
                                    space->windows_map.at(win_id));
                     });
    QObject::connect(ws->session_manager.get(),
                     &win::session_manager::stateChanged,
                     &handler,
                     &KWin::EffectsHandler::sessionStateChanged);
    QObject::connect(vds->qobject.get(),
                     &win::subspace_manager_qobject::countChanged,
                     &handler,
                     &EffectsHandler::numberDesktopsChanged);
    QObject::connect(vds->qobject.get(),
                     &win::subspace_manager_qobject::layoutChanged,
                     &handler,
                     [&handler](int width, int height) {
                         Q_EMIT handler.desktopGridSizeChanged(QSize(width, height));
                         Q_EMIT handler.desktopGridWidthChanged(width);
                         Q_EMIT handler.desktopGridHeightChanged(height);
                     });
    QObject::connect(ws->input->cursor.get(),
                     &std::remove_pointer_t<decltype(ws->input->cursor.get())>::mouse_changed,
                     &handler,
                     &EffectsHandler::mouseChanged);

    auto& base = handler.scene.compositor.platform.base;
    QObject::connect(&base,
                     &Handler::base_t::topology_changed,
                     &handler,
                     [&handler](auto old_topo, auto new_topo) {
                         if (old_topo.size != new_topo.size) {
                             Q_EMIT handler.virtualScreenSizeChanged();
                             Q_EMIT handler.virtualScreenGeometryChanged();
                         }
                     });

    QObject::connect(ws->stacking.order.qobject.get(),
                     &win::stacking_order_qobject::changed,
                     &handler,
                     &EffectsHandler::stackingOrderChanged);

#if KWIN_BUILD_TABBOX
    auto qt_tabbox = ws->tabbox->qobject.get();
    QObject::connect(qt_tabbox,
                     &win::tabbox_qobject::tabbox_added,
                     &handler,
                     [&handler](auto mode) { Q_EMIT handler.tabBoxAdded(static_cast<int>(mode)); });
    QObject::connect(
        qt_tabbox, &win::tabbox_qobject::tabbox_updated, &handler, &EffectsHandler::tabBoxUpdated);
    QObject::connect(
        qt_tabbox, &win::tabbox_qobject::tabbox_closed, &handler, &EffectsHandler::tabBoxClosed);
    QObject::connect(qt_tabbox,
                     &win::tabbox_qobject::tabbox_key_event,
                     &handler,
                     &EffectsHandler::tabBoxKeyEvent);
#endif

    QObject::connect(ws->edges->qobject.get(),
                     &win::screen_edger_qobject::approaching,
                     &handler,
                     [&handler](auto border, auto factor, auto const& geometry) {
                         handler.screenEdgeApproaching(
                             static_cast<ElectricBorder>(border), factor, geometry);
                     });

    auto screen_locker_watcher = ws->base.space->screen_locker_watcher.get();
    using screen_locker_watcher_t = std::remove_pointer_t<decltype(screen_locker_watcher)>;
    QObject::connect(screen_locker_watcher,
                     &screen_locker_watcher_t::locked,
                     &handler,
                     &EffectsHandler::screenLockingChanged);
    QObject::connect(screen_locker_watcher,
                     &screen_locker_watcher_t::about_to_lock,
                     &handler,
                     &EffectsHandler::screenAboutToLock);

    auto make_property_filter = [&handler] {
        using filter = render::x11::property_notify_filter<Handler, typename Handler::space_t>;
        auto& base = handler.scene.compositor.platform.base;
        handler.x11_property_notify
            = std::make_unique<filter>(handler, *base.space, base.x11_data.root_window);
    };

    QObject::connect(&handler.scene.compositor.platform.base,
                     &base::platform::x11_reset,
                     &handler,
                     [&handler, make_property_filter] {
                         handler.registered_atoms.clear();
                         for (auto it = handler.m_propertiesForEffects.keyBegin();
                              it != handler.m_propertiesForEffects.keyEnd();
                              it++) {
                             render::x11::add_support_property(handler, *it);
                         }
                         if (handler.scene.compositor.platform.base.x11_data.connection) {
                             make_property_filter();
                         } else {
                             handler.x11_property_notify.reset();
                         }
                         Q_EMIT handler.xcbConnectionChanged();
                     });

    if (handler.scene.compositor.platform.base.x11_data.connection) {
        make_property_filter();
    }

    // connect all clients
    for (auto& win : ws->windows) {
        // TODO: Can we merge this with the one for Wayland XdgShellClients below?
        std::visit(overload{[&](typename Handler::space_t::x11_window* win) {
                                if (win->control) {
                                    setup_handler_x11_controlled_window_connections(handler, *win);
                                }
                            },
                            [](auto&&) {}},
                   win);
    }
    for (auto win : win::x11::get_unmanageds(*ws)) {
        std::visit(overload{[&](auto&& win) {
                       setup_handler_x11_unmanaged_window_connections(handler, *win);
                   }},
                   win);
    }

    if constexpr (requires { typename Handler::space_t::internal_window_t; }) {
        for (auto& win : ws->windows) {
            std::visit(overload{[&handler](typename Handler::space_t::internal_window_t* win) {
                                    setup_handler_window_connections(handler, *win);
                                },
                                [](auto&&) {}},
                       win);
        }
    }

    QObject::connect(&handler.scene.compositor.platform.base,
                     &Handler::base_t::output_added,
                     &handler,
                     &Handler::slotOutputEnabled);
    QObject::connect(&handler.scene.compositor.platform.base,
                     &Handler::base_t::output_removed,
                     &handler,
                     &Handler::slotOutputDisabled);

    auto const outputs = handler.scene.compositor.platform.base.outputs;
    for (auto&& output : outputs) {
        handler.slotOutputEnabled(output);
    }

    QObject::connect(handler.scene.compositor.platform.base.input->shortcuts.get(),
                     &decltype(handler.scene.compositor.platform.base.input
                                   ->shortcuts)::element_type::keyboard_shortcut_changed,
                     &handler,
                     &Handler::globalShortcutChanged);
}

}
