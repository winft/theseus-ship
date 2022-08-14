/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/redirect.h"
#include "input/window_find.h"
#include "main.h"
#include "toplevel.h"
#include "win/geo.h"
#include "win/internal_window.h"
#include "win/space.h"
#include "win/stacking_order.h"
#include "win/virtual_desktops.h"

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
    QObject::connect(dev->redirect->space.stacking_order->qobject.get(),
                     &win::stacking_order_qobject::changed,
                     dev->qobject.get(),
                     [dev] { device_redirect_update(dev); });
    QObject::connect(dev->redirect->space.qobject.get(),
                     &win::space::qobject_t::clientMinimizedChanged,
                     dev->qobject.get(),
                     [dev] { device_redirect_update(dev); });
    QObject::connect(dev->redirect->space.virtual_desktop_manager->qobject.get(),
                     &win::virtual_desktop_manager_qobject::currentChanged,
                     dev->qobject.get(),
                     [dev] { device_redirect_update(dev); });
}

template<typename Dev>
bool device_redirect_set_at(Dev* dev, Toplevel* window)
{
    if (dev->at.window == window) {
        return false;
    }
    QObject::disconnect(dev->at.notifiers.surface);
    QObject::disconnect(dev->at.notifiers.destroy);

    dev->at.window = window;
    if (window) {
        dev->at.notifiers.destroy = QObject::connect(window->qobject.get(),
                                                     &Toplevel::qobject_t::destroyed,
                                                     dev->qobject.get(),
                                                     [dev] { dev->at.window = nullptr; });
    }
    return true;
}

template<typename Dev>
void device_redirect_set_focus(Dev* dev, Toplevel* window)
{
    QObject::disconnect(dev->focus.notifiers.window_destroy);
    dev->focus.window = window;
    if (window) {
        dev->focus.notifiers.window_destroy = QObject::connect(
            window->qobject.get(), &Toplevel::qobject_t::destroyed, dev->qobject.get(), [dev] {
                dev->focus.window = nullptr;
            });
    }

    // TODO: call focusUpdate?
}

template<typename Dev>
void device_redirect_set_decoration(Dev* dev, win::deco::client_impl* deco)
{
    QObject::disconnect(dev->focus.notifiers.deco_destroy);
    auto old_deco = dev->focus.deco;
    dev->focus.deco = deco;
    if (deco) {
        dev->focus.notifiers.deco_destroy
            = QObject::connect(deco->qobject.get(),
                               &win::deco::client_impl_qobject::destroyed,
                               dev->qobject.get(),
                               [dev] { dev->focus.deco = nullptr; });
    }
    dev->cleanupDecoration(old_deco, dev->focus.deco);
    Q_EMIT dev->qobject->decorationChanged();
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
    auto oldFocus = dev->focus.window;

    if (dev->at.window && !dev->at.window->surface) {
        // The surface has not yet been created (special XWayland case).
        // Therefore listen for its creation.
        if (!dev->at.notifiers.surface) {
            dev->at.notifiers.surface = QObject::connect(dev->at.window->qobject.get(),
                                                         &Toplevel::qobject_t::surfaceChanged,
                                                         dev->qobject.get(),
                                                         [dev] { device_redirect_update(dev); });
        }
        device_redirect_set_focus(dev, nullptr);
    } else {
        device_redirect_set_focus(dev, dev->at.window);
    }

    dev->focusUpdate(oldFocus, dev->focus.window);
}

template<typename Dev>
bool device_redirect_update_decoration(Dev* dev)
{
    auto const old_deco = dev->focus.deco;
    decltype(dev->focus.deco) new_deco{nullptr};

    if (auto win = dev->at.window; win && win->control && win->control->deco.client) {
        auto const client_geo = win::frame_to_client_rect(win, win->frameGeometry());
        if (!client_geo.contains(dev->position().toPoint())) {
            // input device above decoration
            new_deco = win->control->deco.client;
        }
    }

    if (new_deco == old_deco) {
        return false;
    }

    device_redirect_set_decoration(dev, new_deco);
    return true;
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

static QWindow* device_redirect_find_internal_window(std::vector<Toplevel*> const& windows,
                                                     QPoint const& pos)
{
    if (windows.empty()) {
        return nullptr;
    }
    if (kwinApp()->is_screen_locked()) {
        return nullptr;
    }

    auto it = windows.end();
    do {
        --it;
        auto internal = dynamic_cast<win::internal_window*>(*it);
        if (!internal) {
            continue;
        }
        auto w = internal->internalWindow();
        if (!w || !w->isVisible()) {
            continue;
        }
        if (!internal->frameGeometry().contains(pos)) {
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
    } while (it != windows.begin());

    return nullptr;
}

template<typename Dev>
void device_redirect_update(Dev* dev)
{
    Toplevel* toplevel = nullptr;
    QWindow* internal_window = nullptr;

    if (dev->positionValid()) {
        auto const pos = dev->position().toPoint();
        internal_window = device_redirect_find_internal_window(dev->redirect->space.windows, pos);
        if (internal_window) {
            toplevel = dev->redirect->space.findInternal(internal_window);
        } else {
            toplevel = find_window(*dev->redirect, pos);
        }
    }
    // Always set the toplevel at the position of the input device.
    device_redirect_set_at(dev, toplevel);

    if (dev->focusUpdatesBlocked()) {
        return;
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
