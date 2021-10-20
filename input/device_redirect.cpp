/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "device_redirect.h"

#include "redirect.h"

#include "toplevel.h"
#include "virtualdesktops.h"
#include "wayland_server.h"
#include "win/control.h"
#include "win/geo.h"
#include "win/internal_client.h"
#include "win/stacking_order.h"
#include "workspace.h"

namespace KWin::input
{

device_redirect::device_redirect()
    : QObject()
{
}

device_redirect::~device_redirect() = default;

void device_redirect::init()
{
    connect(
        workspace()->stacking_order, &win::stacking_order::changed, this, &device_redirect::update);
    connect(workspace(), &Workspace::clientMinimizedChanged, this, &device_redirect::update);
    connect(VirtualDesktopManager::self(),
            &VirtualDesktopManager::currentChanged,
            this,
            &device_redirect::update);
}

bool device_redirect::setAt(Toplevel* toplevel)
{
    if (m_at.at == toplevel) {
        return false;
    }
    disconnect(m_at.surfaceCreatedConnection);
    m_at.surfaceCreatedConnection = QMetaObject::Connection();

    m_at.at = toplevel;
    return true;
}

void device_redirect::setFocus(Toplevel* toplevel)
{
    m_focus.focus = toplevel;
    // TODO: call focusUpdate?
}

void device_redirect::setDecoration(Decoration::DecoratedClientImpl* decoration)
{
    auto oldDeco = m_focus.decoration;
    m_focus.decoration = decoration;
    cleanupDecoration(oldDeco.data(), m_focus.decoration.data());
    emit decorationChanged();
}

void device_redirect::setInternalWindow(QWindow* window)
{
    m_focus.internalWindow = window;
    // TODO: call internalWindowUpdate?
}

void device_redirect::updateFocus()
{
    auto oldFocus = m_focus.focus;

    if (m_at.at && !m_at.at->surface()) {
        // The surface has not yet been created (special XWayland case).
        // Therefore listen for its creation.
        if (!m_at.surfaceCreatedConnection) {
            m_at.surfaceCreatedConnection
                = connect(m_at.at, &Toplevel::surfaceChanged, this, &device_redirect::update);
        }
        m_focus.focus = nullptr;
    } else {
        m_focus.focus = m_at.at;
    }

    focusUpdate(oldFocus, m_focus.focus);
}

bool device_redirect::updateDecoration()
{
    const auto oldDeco = m_focus.decoration.data();
    m_focus.decoration = nullptr;

    auto ac = m_at.at.data();
    if (ac && ac->control && ac->control->deco().client) {
        auto const client_geo = win::frame_to_client_rect(ac, ac->frameGeometry());
        if (!client_geo.contains(position().toPoint())) {
            // input device above decoration
            m_focus.decoration = ac->control->deco().client;
        }
    }

    if (m_focus.decoration == oldDeco) {
        // no change to decoration
        return false;
    }
    cleanupDecoration(oldDeco, m_focus.decoration.data());
    emit decorationChanged();
    return true;
}

void device_redirect::updateInternalWindow(QWindow* window)
{
    if (m_focus.internalWindow == window) {
        // no change
        return;
    }
    const auto oldInternal = m_focus.internalWindow;
    m_focus.internalWindow = window;
    cleanupInternalWindow(oldInternal, window);
}

void device_redirect::update()
{
    if (!m_inited) {
        return;
    }

    Toplevel* toplevel = nullptr;
    QWindow* internalWindow = nullptr;

    if (positionValid()) {
        const auto pos = position().toPoint();
        internalWindow = findInternalWindow(pos);
        if (internalWindow) {
            toplevel = workspace()->findInternal(internalWindow);
        } else {
            toplevel = kwinApp()->input->redirect->findToplevel(pos);
        }
    }
    // Always set the toplevel at the position of the input device.
    setAt(toplevel);

    if (focusUpdatesBlocked()) {
        return;
    }

    if (internalWindow) {
        if (m_focus.internalWindow != internalWindow) {
            // changed internal window
            updateDecoration();
            updateInternalWindow(internalWindow);
            updateFocus();
        } else if (updateDecoration()) {
            // went onto or off from decoration, update focus
            updateFocus();
        }
        return;
    }
    updateInternalWindow(nullptr);

    if (m_focus.focus != m_at.at) {
        // focus change
        updateDecoration();
        updateFocus();
        return;
    }
    // check if switched to/from decoration while staying on the same Toplevel
    if (updateDecoration()) {
        // went onto or off from decoration, update focus
        updateFocus();
    }
}

Toplevel* device_redirect::at() const
{
    return m_at.at.data();
}

Toplevel* device_redirect::focus() const
{
    return m_focus.focus.data();
}

Decoration::DecoratedClientImpl* device_redirect::decoration() const
{
    return m_focus.decoration;
}

QWindow* device_redirect::internalWindow() const
{
    return m_focus.internalWindow;
}

QWindow* device_redirect::findInternalWindow(const QPoint& pos) const
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
        auto internal = qobject_cast<win::InternalClient*>(*it);
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
        const QRegion mask = w->mask().translated(w->geometry().topLeft());
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

}
