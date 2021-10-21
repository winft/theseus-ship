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

#include "../platform.h"
#include "effects.h"
#include "global_shortcuts_manager.h"
#include "main.h"
#include "screens.h"
#include "toplevel.h"
#include "utils.h"
#include "win/geo.h"
#include "win/stacking_order.h"
#include "win/wayland/input.h"
#include "workspace.h"

namespace KWin::input
{

redirect::redirect(keyboard_redirect* keyboard,
                   pointer_redirect* pointer,
                   tablet_redirect* tablet,
                   touch_redirect* touch)
    : m_keyboard(keyboard)
    , m_pointer(pointer)
    , m_tablet(tablet)
    , m_touch(touch)
    , m_shortcuts(new global_shortcuts_manager(this))
{
    qRegisterMetaType<KWin::input::redirect::KeyboardKeyState>();
    qRegisterMetaType<KWin::input::redirect::PointerButtonState>();
    qRegisterMetaType<KWin::input::redirect::PointerAxis>();
    connect(kwinApp(), &Application::startup_finished, this, &redirect::setupWorkspace);
}

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

void redirect::installInputEventFilter(event_filter* filter)
{
    Q_ASSERT(!contains(m_filters, filter));
    m_filters.push_back(filter);
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

void redirect::setupWorkspace()
{
    setupInputFilters();
}

void redirect::setupInputFilters()
{
}

void redirect::processPointerMotion(const QPointF& pos, uint32_t time)
{
    m_pointer->processMotion(pos, time);
}

void redirect::processPointerButton(uint32_t button,
                                    redirect::PointerButtonState state,
                                    uint32_t time)
{
    m_pointer->process_button(
        {button,
         state == PointerButtonPressed ? button_state::pressed : button_state::released,
         {nullptr, time}});
}

void redirect::processPointerAxis(axis_orientation orientation,
                                  double delta,
                                  int32_t discreteDelta,
                                  axis_source source,
                                  uint32_t time)
{
    m_pointer->process_axis({source, orientation, delta, discreteDelta, nullptr, time});
}

void redirect::processKeyboardKey(uint32_t key, redirect::KeyboardKeyState state, uint32_t time)
{
    m_keyboard->process_key({key,
                             state == KeyboardKeyState::KeyboardKeyPressed ? button_state::pressed
                                                                           : button_state::released,
                             false,
                             nullptr,
                             time});
}

void redirect::processKeyboardModifiers(uint32_t modsDepressed,
                                        uint32_t modsLatched,
                                        uint32_t modsLocked,
                                        uint32_t group)
{
    m_keyboard->processModifiers(modsDepressed, modsLatched, modsLocked, group);
}

void redirect::processKeymapChange(int fd, uint32_t size)
{
    m_keyboard->processKeymapChange(fd, size);
}

void redirect::processTouchDown(qint32 id, const QPointF& pos, quint32 time)
{
    m_touch->processDown(id, pos, time);
}

void redirect::processTouchUp(qint32 id, quint32 time)
{
    m_touch->processUp(id, time);
}

void redirect::processTouchMotion(qint32 id, const QPointF& pos, quint32 time)
{
    m_touch->processMotion(id, pos, time);
}

void redirect::cancelTouch()
{
    m_touch->cancel();
}

void redirect::touchFrame()
{
    m_touch->frame();
}

Qt::MouseButtons redirect::qtButtonStates() const
{
    return m_pointer->buttons();
}

Toplevel* redirect::findToplevel(const QPoint& pos)
{
    if (!Workspace::self()) {
        return nullptr;
    }

    // TODO: check whether the unmanaged wants input events at all
    if (!kwinApp()->is_screen_locked()) {
        // if an effect overrides the cursor we don't have a window to focus
        if (effects && static_cast<EffectsHandlerImpl*>(effects)->isMouseInterception()) {
            return nullptr;
        }
        auto const& unmanaged = Workspace::self()->unmanagedList();
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
    if (!Workspace::self()) {
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
            if (!window->isOnCurrentActivity() || !window->isOnCurrentDesktop()
                || window->control->minimized()) {
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

Qt::KeyboardModifiers redirect::keyboardModifiers() const
{
    return m_keyboard->modifiers();
}

Qt::KeyboardModifiers redirect::modifiersRelevantForGlobalShortcuts() const
{
    return m_keyboard->modifiersRelevantForGlobalShortcuts();
}

void redirect::registerShortcut(const QKeySequence& shortcut, QAction* action)
{
    Q_UNUSED(shortcut)
    kwinApp()->platform->setupActionForGlobalAccel(action);
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

void redirect::warpPointer(const QPointF& pos)
{
    m_pointer->warp(pos);
}

bool redirect::supportsPointerWarping() const
{
    return m_pointer->supportsWarping();
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
