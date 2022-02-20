/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "redirect.h"

#include "keyboard.h"
#include "pointer.h"
#include "switch.h"
#include "touch.h"

#include "event_filter.h"
#include "event_spy.h"
#include "keyboard_redirect.h"
#include "pointer_redirect.h"
#include "tablet_redirect.h"
#include "touch_redirect.h"

#include "global_shortcuts_manager.h"
#include "main.h"
#include "render/effects.h"
#include "render/platform.h"
#include "toplevel.h"
#include "win/geo.h"
#include "win/space.h"
#include "win/stacking_order.h"
#include "win/wayland/input.h"

namespace KWin::input
{

redirect::~redirect()
{
    auto const filters = m_filters;
    for (auto filter : filters) {
        delete filter;
    }

    auto const spies = m_spies;
    for (auto spy : spies) {
        delete spy;
    }
}

void redirect::append_filter(event_filter* filter)
{
    Q_ASSERT(!contains(m_filters, filter));
    m_filters.insert(m_filters_install_iterator, filter);
}

void redirect::prependInputEventFilter(event_filter* filter)
{
    Q_ASSERT(!contains(m_filters, filter));
    m_filters.insert(m_filters.begin(), filter);
}

void redirect::uninstallInputEventFilter(event_filter* filter)
{
    remove_all(m_filters, filter);
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
    if (!workspace()) {
        return nullptr;
    }

    // TODO: check whether the unmanaged wants input events at all
    if (!kwinApp()->is_screen_locked()) {
        // if an effect overrides the cursor we don't have a window to focus
        if (effects && static_cast<render::effects_handler_impl*>(effects)->isMouseInterception()) {
            return nullptr;
        }
        auto const& unmanaged = workspace()->unmanagedList();
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
    if (!workspace()) {
        return nullptr;
    }
    auto const isScreenLocked = kwinApp()->is_screen_locked();
    auto const& stacking = workspace()->stacking_order->sorted();
    if (stacking.empty()) {
        return nullptr;
    }
    auto it = stacking.end();
    do {
        --it;
        auto window = *it;
        if (window->isDeleted()) {
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
        if (!window->readyForPainting()) {
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

void redirect::registerShortcut(const QKeySequence& shortcut, QAction* action)
{
    Q_UNUSED(shortcut)
    kwinApp()->input->setup_action_for_global_accel(action);
}

void redirect::registerPointerShortcut(Qt::KeyboardModifiers modifiers,
                                       Qt::MouseButton pointerButtons,
                                       QAction* action)
{
    m_shortcuts->registerPointerShortcut(action, modifiers, pointerButtons);
}

void redirect::registerAxisShortcut(Qt::KeyboardModifiers modifiers,
                                    PointerAxisDirection axis,
                                    QAction* action)
{
    m_shortcuts->registerAxisShortcut(action, modifiers, axis);
}

void redirect::registerTouchpadSwipeShortcut(SwipeDirection direction, QAction* action)
{
    m_shortcuts->registerTouchpadSwipe(action, direction);
}

void redirect::registerGlobalAccel(KGlobalAccelInterface* interface)
{
    m_shortcuts->setKGlobalAccelInterface(interface);
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
