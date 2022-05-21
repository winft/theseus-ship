/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/redirect.h"
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
    QObject::connect(workspace()->stacking_order, &win::stacking_order::changed, dev, [dev] {
        device_redirect_update(dev);
    });
    QObject::connect(workspace(), &win::space::clientMinimizedChanged, dev, [dev] {
        device_redirect_update(dev);
    });
    QObject::connect(win::virtual_desktop_manager::self(),
                     &win::virtual_desktop_manager::currentChanged,
                     dev,
                     [dev] { device_redirect_update(dev); });
}

template<typename Dev>
bool device_redirect_set_at(Dev* dev, Toplevel* toplevel)
{
    if (dev->at.window == toplevel) {
        return false;
    }
    QObject::disconnect(dev->at.surface_notifier);
    dev->at.surface_notifier = QMetaObject::Connection();

    dev->at.window = toplevel;
    return true;
}

template<typename Dev>
void device_redirect_set_focus(Dev* dev, Toplevel* toplevel)
{
    dev->focus.window = toplevel;
    // TODO: call focusUpdate?
}

template<typename Dev>
void device_redirect_set_decoration(Dev* dev, win::deco::client_impl* decoration)
{
    auto old_deco = dev->focus.deco;
    dev->focus.deco = decoration;
    dev->cleanupDecoration(old_deco.data(), dev->focus.deco.data());
    Q_EMIT dev->decorationChanged();
}

template<typename Dev>
void device_redirect_set_internal_window(Dev* dev, QWindow* window)
{
    dev->focus.internal_window = window;
    // TODO: call internalWindowUpdate?
}

template<typename Dev>
void device_redirect_update_focus(Dev* dev)
{
    auto oldFocus = dev->focus.window;

    if (dev->at.window && !dev->at.window->surface()) {
        // The surface has not yet been created (special XWayland case).
        // Therefore listen for its creation.
        if (!dev->at.surface_notifier) {
            dev->at.surface_notifier
                = QObject::connect(dev->at.window, &Toplevel::surfaceChanged, dev, [dev] {
                      device_redirect_update(dev);
                  });
        }
        dev->focus.window = nullptr;
    } else {
        dev->focus.window = dev->at.window;
    }

    dev->focusUpdate(oldFocus, dev->focus.window);
}

template<typename Dev>
bool device_redirect_update_decoration(Dev* dev)
{
    const auto oldDeco = dev->focus.deco.data();
    dev->focus.deco = nullptr;

    auto ac = dev->at.window.data();
    if (ac && ac->control && ac->control->deco().client) {
        auto const client_geo = win::frame_to_client_rect(ac, ac->frameGeometry());
        if (!client_geo.contains(dev->position().toPoint())) {
            // input device above decoration
            dev->focus.deco = ac->control->deco().client;
        }
    }

    if (dev->focus.deco == oldDeco) {
        // no change to decoration
        return false;
    }

    dev->cleanupDecoration(oldDeco, dev->focus.deco.data());
    Q_EMIT dev->decorationChanged();
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

static QWindow* device_redirect_find_internal_window(QPoint const& pos)
{
    if (kwinApp()->is_screen_locked()) {
        return nullptr;
    }

    auto const& windows = workspace()->windows();
    if (windows.empty()) {
        return nullptr;
    }

    auto it = windows.end();
    do {
        --it;
        auto internal = qobject_cast<win::internal_window*>(*it);
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
        internal_window = device_redirect_find_internal_window(pos);
        if (internal_window) {
            toplevel = workspace()->findInternal(internal_window);
        } else {
            toplevel = kwinApp()->input->redirect->findToplevel(pos);
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
