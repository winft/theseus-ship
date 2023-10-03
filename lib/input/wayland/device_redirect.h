/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/window_find.h"
#include "win/geo.h"
#include "win/space_qobject.h"
#include "win/stacking_order.h"

#include <QWindow>

namespace KWin
{
namespace input::wayland
{

template<typename Dev>
void device_redirect_update(Dev* dev);

template<typename Dev>
void device_redirect_init(Dev* dev)
{
    auto& space = dev->redirect->space;
    QObject::connect(space.stacking.order.qobject.get(),
                     &win::stacking_order_qobject::changed,
                     dev->qobject.get(),
                     [dev] { device_redirect_update(dev); });
    QObject::connect(space.qobject.get(),
                     &win::space_qobject::clientMinimizedChanged,
                     dev->qobject.get(),
                     [dev] { device_redirect_update(dev); });
    QObject::connect(space.subspace_manager->qobject.get(),
                     &decltype(space.subspace_manager->qobject)::element_type::current_changed,
                     dev->qobject.get(),
                     [dev] { device_redirect_update(dev); });
}

template<typename Dev>
void device_redirect_set_at(Dev* dev, decltype(dev->at.window) window)
{
    if (dev->at.window == window) {
        return;
    }

    QObject::disconnect(dev->at.notifiers.surface);
    QObject::disconnect(dev->at.notifiers.destroy);

    dev->at.window = window;

    if (window) {
        std::visit(overload{[dev](auto&& win) {
                       dev->at.notifiers.destroy = QObject::connect(win->qobject.get(),
                                                                    &win::window_qobject::destroyed,
                                                                    dev->qobject.get(),
                                                                    [dev] { dev->at.window = {}; });
                   }},
                   *window);
    }
}

template<typename Dev>
void device_redirect_unset_focus(Dev* dev)
{
    QObject::disconnect(dev->focus.notifiers.window_destroy);
    dev->focus.window = {};
    // TODO: call focusUpdate?
}

template<typename Dev, typename Win>
void device_redirect_set_focus(Dev* dev, Win& window)
{
    device_redirect_unset_focus(dev);
    dev->focus.window = &window;
    dev->focus.notifiers.window_destroy = QObject::connect(window.qobject.get(),
                                                           &win::window_qobject::destroyed,
                                                           dev->qobject.get(),
                                                           [dev] { dev->focus.window = {}; });
    // TODO: call focusUpdate?
}

template<typename Dev>
void device_redirect_set_internal_window(Dev* dev, QWindow* window)
{
    QObject::disconnect(dev->focus.notifiers.internal_window_destroy);
    dev->focus.internal_window = window;
    if (window) {
        dev->focus.notifiers.window_destroy
            = QObject::connect(window, &QWindow::destroyed, dev->qobject.get(), [dev] {
                  dev->focus.internal_window = nullptr;
              });
    }

    // TODO: call internalWindowUpdate?
}

template<typename Dev>
void device_redirect_update_focus(Dev* dev)
{
    using space_t = std::decay_t<decltype(dev->redirect->space)>;

    auto oldFocus = dev->focus.window;

    if (dev->at.window) {
        std::visit(overload{[&](typename space_t::wayland_window* win) {
                                device_redirect_set_focus(dev, *win);
                            },
                            [&](typename space_t::x11_window* win) {
                                if (win->surface) {
                                    device_redirect_set_focus(dev, *win);
                                    return;
                                }

                                // The surface has not yet been created (special XWayland case).
                                // Therefore listen for its creation.
                                if (!dev->at.notifiers.surface) {
                                    dev->at.notifiers.surface
                                        = QObject::connect(win->qobject.get(),
                                                           &win::window_qobject::surfaceChanged,
                                                           dev->qobject.get(),
                                                           [dev] { device_redirect_update(dev); });
                                }
                                device_redirect_unset_focus(dev);
                            },
                            [](auto&&) { /* internal window */ }},
                   *dev->at.window);
    } else {
        device_redirect_unset_focus(dev);
    }

    // TODO(romangg): If the window is the same should we skip? Would simplify the code.
    dev->focusUpdate(oldFocus, dev->focus.window);
}

template<typename Dev>
void device_redirect_unset_deco(Dev* dev)
{
    QObject::disconnect(dev->focus.notifiers.deco_destroy);

    if constexpr (requires(Dev dev) { dev.unset_deco(); }) {
        if (dev->focus.deco.client) {
            dev->unset_deco();
        }
    }

    dev->focus.deco = {};
}

template<typename Dev>
bool device_redirect_update_decoration(Dev* dev)
{
    auto const old_deco = dev->focus.deco.client;

    if (!dev->at.window) {
        if (!old_deco) {
            return false;
        }

        device_redirect_unset_deco(dev);
        return true;
    }

    return std::visit(overload{[&](auto&& win) {
                          decltype(win->control->deco.client) new_deco{nullptr};
                          if (win->control && win->control->deco.client) {
                              auto const geo = win::frame_to_client_rect(win, win->geo.frame);
                              if (!geo.contains(dev->position().toPoint())) {
                                  // input device above decoration
                                  new_deco = win->control->deco.client;
                              }
                          }

                          if (new_deco == old_deco) {
                              return false;
                          }

                          device_redirect_unset_deco(dev);

                          if (new_deco) {
                              dev->focus.notifiers.deco_destroy
                                  = QObject::connect(new_deco->qobject.get(),
                                                     &win::deco::client_impl_qobject::destroyed,
                                                     dev->qobject.get(),
                                                     [dev] { dev->focus.deco = {}; });
                              if constexpr (requires(Dev dev) { dev.unset_deco(); }) {
                                  dev->set_deco(*new_deco);
                              }
                          }

                          dev->focus.deco.client = new_deco;
                          dev->focus.deco.window = win;
                          return true;
                      }},
                      *dev->at.window);
}

template<typename Dev>
void device_redirect_update_internal_window(Dev* dev, QWindow* window)
{
    if (dev->focus.internal_window == window) {
        // no change
        return;
    }
    auto const old_internal = dev->focus.internal_window;
    dev->focus.internal_window = window;
    dev->cleanupInternalWindow(old_internal, window);
}

template<typename Space>
QWindow* device_redirect_find_internal_window(Space& space, QPoint const& pos)
{
    using int_win = typename Space::internal_window_t;

    if (space.windows.empty()) {
        return nullptr;
    }
    if (base::wayland::is_screen_locked(space.base)) {
        return nullptr;
    }

    auto it = space.windows.end();

    do {
        --it;

        if (!std::holds_alternative<int_win*>(*it)) {
            continue;
        }

        auto internal = std::get<int_win*>(*it);
        auto w = internal->internalWindow();
        if (!w || !w->isVisible()) {
            continue;
        }

        if (!internal->geo.frame.contains(pos)) {
            continue;
        }

        // check input mask
        auto const mask = w->mask().translated(w->geometry().topLeft());
        if (!mask.isEmpty() && !mask.contains(pos)) {
            continue;
        }
        if (w->property("outputOnly").toBool()) {
            continue;
        }
        return w;
    } while (it != space.windows.begin());

    return nullptr;
}

template<typename Dev>
void device_redirect_update(Dev* dev)
{
    using space_t = typename std::remove_pointer_t<decltype(dev->redirect)>::space_t;

    std::optional<typename space_t::window_t> toplevel;
    QWindow* internal_window = nullptr;

    auto position_valid{true};
    if constexpr (requires(Dev dev) { dev.positionValid(); }) {
        position_valid = dev->positionValid();
    }
    if (position_valid) {
        auto const pos = dev->position().toPoint();
        auto& space = dev->redirect->space;
        internal_window = device_redirect_find_internal_window(space, pos);
        if (internal_window) {
            toplevel = space.findInternal(internal_window);
        } else {
            toplevel = find_window(*dev->redirect, pos);
        }
    }

    // Always set the toplevel at the position of the input device.
    device_redirect_set_at(dev, toplevel);

    if constexpr (requires(Dev dev) { dev.focusUpdatesBlocked(); }) {
        if (dev->focusUpdatesBlocked()) {
            return;
        }
    }

    if (internal_window) {
        if (dev->focus.internal_window != internal_window) {
            // changed internal window
            device_redirect_update_decoration(dev);
            device_redirect_update_internal_window(dev, internal_window);
            device_redirect_update_focus(dev);
        } else if (device_redirect_update_decoration(dev)) {
            // went onto or off from decoration, update focus
            device_redirect_update_focus(dev);
        }
        return;
    }
    device_redirect_update_internal_window(dev, nullptr);

    if (dev->focus.window != dev->at.window) {
        // focus change
        device_redirect_update_decoration(dev);
        device_redirect_update_focus(dev);
        return;
    }
    // check if switched to/from decoration while staying on the same Toplevel
    if (device_redirect_update_decoration(dev)) {
        // went onto or off from decoration, update focus
        device_redirect_update_focus(dev);
    }
}

}
}
