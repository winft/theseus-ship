/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "redirect.h"

#include "keyboard.h"
#include "pointer.h"
#include "switch.h"
#include "touch.h"

#include "event_spy.h"
#include "keyboard_redirect.h"
#include "pointer_redirect.h"
#include "tablet_redirect.h"
#include "touch_redirect.h"

#include "main.h"
#include "render/compositor.h"
#include "render/effects.h"
#include "render/platform.h"
#include "toplevel.h"
#include "win/geo.h"
#include "win/space.h"
#include "win/stacking_order.h"
#include "win/wayland/input.h"
#include "win/x11/unmanaged.h"

namespace KWin::input
{

redirect::redirect(input::platform& platform, win::space& space)
    : platform{platform}
    , space{space}
{
    platform.redirect = this;
}

redirect::~redirect()
{
    auto const spies = m_spies;
    for (auto spy : spies) {
        delete spy;
    }
}

void redirect::installInputEventSpy(event_spy* spy)
{
    m_spies.push_back(spy);
}

void redirect::uninstallInputEventSpy(event_spy* spy)
{
    remove_all(m_spies, spy);
}

void redirect::cancelTouch()
{
    m_touch->cancel();
}

Qt::MouseButtons redirect::qtButtonStates() const
{
    return m_pointer->buttons();
}

Toplevel* redirect::findToplevel(const QPoint& pos)
{
    // TODO: check whether the unmanaged wants input events at all
    if (!kwinApp()->is_screen_locked()) {
        // if an effect overrides the cursor we don't have a window to focus
        if (space.render.effects && space.render.effects->isMouseInterception()) {
            return nullptr;
        }
        auto const& unmanaged = win::x11::get_unmanageds<Toplevel>(space);
        for (auto const& u : unmanaged) {
            if (win::input_geometry(u).contains(pos) && win::wayland::accepts_input(u, pos)) {
                return u;
            }
        }
    }
    return findManagedToplevel(pos);
}

Toplevel* redirect::findManagedToplevel(const QPoint& pos)
{
    auto const isScreenLocked = kwinApp()->is_screen_locked();
    auto const& stacking = space.stacking_order->stack;
    if (stacking.empty()) {
        return nullptr;
    }
    auto it = stacking.end();
    do {
        --it;
        auto window = *it;
        if (window->remnant) {
            // a deleted window doesn't get mouse events
            continue;
        }
        if (window->control) {
            if (!window->isOnCurrentDesktop() || window->control->minimized()) {
                continue;
            }
        }
        if (window->isHiddenInternal()) {
            continue;
        }
        if (!window->ready_for_painting) {
            continue;
        }
        if (isScreenLocked) {
            if (!window->isLockScreen() && !window->isInputMethod()) {
                continue;
            }
        }
        if (win::input_geometry(window).contains(pos) && win::wayland::accepts_input(window, pos)) {
            return window;
        }
    } while (it != stacking.begin());
    return nullptr;
}

QPointF redirect::globalPointer() const
{
    return m_pointer->pos();
}

void redirect::startInteractiveWindowSelection(std::function<void(KWin::Toplevel*)> callback,
                                               QByteArray const& /*cursorName*/)
{
    callback(nullptr);
}

void redirect::startInteractivePositionSelection(std::function<void(QPoint const&)> callback)
{
    callback(QPoint(-1, -1));
}

bool redirect::isSelectingWindow() const
{
    return false;
}

}
