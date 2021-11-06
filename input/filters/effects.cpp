/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "effects.h"

#include "helpers.h"

#include "../../effects.h"
#include "wayland_server.h"
#include <input/qt_event.h>

#include <Wrapland/Server/seat.h>

namespace KWin::input
{

bool effects_filter::button(button_event const& event)
{
    if (!effects) {
        return false;
    }
    auto qt_event = button_to_qt_event(event);
    return static_cast<EffectsHandlerImpl*>(effects)->checkInputWindowEvent(&qt_event);
}

bool effects_filter::motion(motion_event const& event)
{
    if (!effects) {
        return false;
    }
    auto qt_event = motion_to_qt_event(event);
    return static_cast<EffectsHandlerImpl*>(effects)->checkInputWindowEvent(&qt_event);
}

bool effects_filter::axis(axis_event const& event)
{
    if (!effects) {
        return false;
    }
    auto qt_event = axis_to_qt_event(event);
    return static_cast<EffectsHandlerImpl*>(effects)->checkInputWindowEvent(&qt_event);
}

bool effects_filter::key(key_event const& event)
{
    if (!effects || !static_cast<EffectsHandlerImpl*>(effects)->hasKeyboardGrab()) {
        return false;
    }
    waylandServer()->seat()->setFocusedKeyboardSurface(nullptr);
    pass_to_wayland_server(event);
    auto qt_event = key_to_qt_event(event);
    static_cast<EffectsHandlerImpl*>(effects)->grabbedKeyboardEvent(&qt_event);
    return true;
}

bool effects_filter::key_repeat(key_event const& event)
{
    if (!effects || !static_cast<EffectsHandlerImpl*>(effects)->hasKeyboardGrab()) {
        return false;
    }
    auto qt_event = key_to_qt_event(event);
    static_cast<EffectsHandlerImpl*>(effects)->grabbedKeyboardEvent(&qt_event);
    return true;
}

bool effects_filter::touch_down(touch_down_event const& event)
{
    if (!effects) {
        return false;
    }
    return static_cast<EffectsHandlerImpl*>(effects)->touchDown(
        event.id, event.pos, event.base.time_msec);
}

bool effects_filter::touch_motion(touch_motion_event const& event)
{
    if (!effects) {
        return false;
    }
    return static_cast<EffectsHandlerImpl*>(effects)->touchMotion(
        event.id, event.pos, event.base.time_msec);
}

bool effects_filter::touch_up(touch_up_event const& event)
{
    if (!effects) {
        return false;
    }
    return static_cast<EffectsHandlerImpl*>(effects)->touchUp(event.id, event.base.time_msec);
}

}
