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
    if (dev->m_at.at == toplevel) {
        return false;
    }
    QObject::disconnect(dev->m_at.surface_notifier);
    dev->m_at.surface_notifier = QMetaObject::Connection();

    dev->m_at.at = toplevel;
    return true;
}

template<typename Dev>
void device_redirect_set_focus(Dev* dev, Toplevel* toplevel)
{
    dev->m_focus.focus = toplevel;
    // TODO: call focusUpdate?
}

template<typename Dev>
void device_redirect_set_decoration(Dev* dev, Decoration::DecoratedClientImpl* decoration)
{
    auto old_deco = dev->m_focus.decoration;
    dev->m_focus.decoration = decoration;
    dev->cleanupDecoration(old_deco.data(), dev->m_focus.decoration.data());
    Q_EMIT dev->decorationChanged();
}

template<typename Dev>
void device_redirect_set_internal_window(Dev* dev, QWindow* window)
{
    dev->m_focus.internalWindow = window;
    // TODO: call internalWindowUpdate?
}

template<typename Dev>
void device_redirect_update_focus(Dev* dev)
{
    auto oldFocus = dev->m_focus.focus;

    if (dev->m_at.at && !dev->m_at.at->surface()) {
        // The surface has not yet been created (special XWayland case).
        // Therefore listen for its creation.
        if (!dev->m_at.surface_notifier) {
            dev->m_at.surface_notifier
                = QObject::connect(dev->m_at.at, &Toplevel::surfaceChanged, dev, [dev] {
                      device_redirect_update(dev);
                  });
        }
        dev->m_focus.focus = nullptr;
    } else {
        dev->m_focus.focus = dev->m_at.at;
    }

    dev->focusUpdate(oldFocus, dev->m_focus.focus);
}

template<typename Dev>
bool device_redirect_update_decoration(Dev* dev)
{
    const auto oldDeco = dev->m_focus.decoration.data();
    dev->m_focus.decoration = nullptr;

    auto ac = dev->m_at.at.data();
    if (ac && ac->control && ac->control->deco().client) {
        auto const client_geo = win::frame_to_client_rect(ac, ac->frameGeometry());
        if (!client_geo.contains(dev->position().toPoint())) {
            // input device above decoration
            dev->m_focus.decoration = ac->control->deco().client;
        }
    }

    if (dev->m_focus.decoration == oldDeco) {
        // no change to decoration
        return false;
    }

    dev->cleanupDecoration(oldDeco, dev->m_focus.decoration.data());
    Q_EMIT dev->decorationChanged();
    return true;
}

template<typename Dev>
void device_redirect_update_internal_window(Dev* dev, QWindow* window)
{
    if (dev->m_focus.internalWindow == window) {
        // no change
        return;
    }
    auto const old_internal = dev->m_focus.internalWindow;
    dev->m_focus.internalWindow = window;
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
        if (dev->m_focus.internalWindow != internal_window) {
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

    if (dev->m_focus.focus != dev->m_at.at) {
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
