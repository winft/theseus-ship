/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <render/effect/setup_window.h>
#include <win/screen_edges.h>

#include <config-kwin.h>

#if KWIN_BUILD_TABBOX
#include <win/tabbox/tabbox.h>
#endif

#include <QObject>
#include <variant>

namespace KWin::effect
{

template<typename Handler>
void setup_handler(Handler& handler)
{
    QObject::connect(&handler, &Handler::hasActiveFullScreenEffectChanged, &handler, [&handler] {
        Q_EMIT handler.scene.platform.base.space->edges->qobject->checkBlocking();
    });

    auto ws = handler.scene.platform.base.space.get();
    auto& vds = ws->subspace_manager;

    QObject::connect(ws->qobject.get(),
                     &win::space_qobject::showingDesktopChanged,
                     &handler,
                     &Handler::showingDesktopChanged);
    QObject::connect(ws->qobject.get(),
                     &win::space_qobject::current_subspace_changed,
                     &handler,
                     [&handler, space = ws](auto old) {
                         auto const current = space->subspace_manager->current;
                         if (!old || current == old) {
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
                         Q_EMIT handler.desktopChanged(
                             old->x11DesktopNumber(), current->x11DesktopNumber(), eff_win);
                     });
    QObject::connect(ws->qobject.get(),
                     &win::space_qobject::current_subspace_changing,
                     &handler,
                     [&handler, space = ws](auto current, QPointF offset) {
                         EffectWindow* eff_win{nullptr};
                         if (auto& mov_res = space->move_resize_window) {
                             std::visit(overload{[&](auto&& win) {
                                            assert(win->render);
                                            assert(win->render->effect);
                                            eff_win = win->render->effect.get();
                                        }},
                                        *mov_res);
                         }
                         Q_EMIT handler.desktopChanging(
                             current->x11DesktopNumber(), offset, eff_win);
                     });
    QObject::connect(ws->qobject.get(),
                     &win::space_qobject::current_subspace_changing_cancelled,
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
                                        setup_window_connections(*win);
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

    if constexpr (requires(decltype(ws) space) { space->session_manager; }) {
        QObject::connect(ws->session_manager.get(),
                         &decltype(ws->session_manager)::element_type::stateChanged,
                         &handler,
                         &KWin::EffectsHandler::sessionStateChanged);
    }

    QObject::connect(vds->qobject.get(),
                     &decltype(vds->qobject)::element_type::countChanged,
                     &handler,
                     &EffectsHandler::numberDesktopsChanged);
    QObject::connect(vds->qobject.get(),
                     &decltype(vds->qobject)::element_type::layoutChanged,
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

    auto& base = handler.scene.platform.base;
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

    auto screen_locker_watcher = ws->base.space->desktop->screen_locker_watcher.get();
    using screen_locker_watcher_t = std::remove_pointer_t<decltype(screen_locker_watcher)>;
    QObject::connect(screen_locker_watcher,
                     &screen_locker_watcher_t::locked,
                     &handler,
                     &EffectsHandler::screenLockingChanged);
    QObject::connect(screen_locker_watcher,
                     &screen_locker_watcher_t::about_to_lock,
                     &handler,
                     &EffectsHandler::screenAboutToLock);

    if constexpr (requires { typename Handler::space_t::internal_window_t; }) {
        for (auto& win : ws->windows) {
            std::visit(overload{[&handler](typename Handler::space_t::internal_window_t* win) {
                                    setup_window_connections(*win);
                                },
                                [](auto&&) {}},
                       win);
        }
    }

    QObject::connect(&handler.scene.platform.base,
                     &Handler::base_t::output_added,
                     &handler,
                     &Handler::slotOutputEnabled);
    QObject::connect(&handler.scene.platform.base,
                     &Handler::base_t::output_removed,
                     &handler,
                     &Handler::slotOutputDisabled);

    auto const outputs = handler.scene.platform.base.outputs;
    for (auto&& output : outputs) {
        handler.slotOutputEnabled(output);
    }

    QObject::connect(handler.scene.platform.base.input->shortcuts.get(),
                     &decltype(handler.scene.platform.base.input
                                   ->shortcuts)::element_type::keyboard_shortcut_changed,
                     &handler,
                     &Handler::globalShortcutChanged);
}

}
