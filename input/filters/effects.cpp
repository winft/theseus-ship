/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "effects.h"

#include "helpers.h"

#include "base/wayland/server.h"
#include "input/qt_event.h"
#include "main.h"
#include "render/effects.h"

#include <Wrapland/Server/seat.h>

namespace KWin::input
{

bool effects_filter::button(button_event const& event)
{
    if (!effects) {
        return false;
    }
    auto qt_event = button_to_qt_event(event);
    return static_cast<render::effects_handler_impl*>(effects)->checkInputWindowEvent(&qt_event);
}

bool effects_filter::motion(motion_event const& event)
{
    if (!effects) {
        return false;
    }
    auto qt_event = motion_to_qt_event(event);
    return static_cast<render::effects_handler_impl*>(effects)->checkInputWindowEvent(&qt_event);
}

bool effects_filter::axis(axis_event const& event)
{
    if (!effects) {
        return false;
    }
    auto qt_event = axis_to_qt_event(event);
    return static_cast<render::effects_handler_impl*>(effects)->checkInputWindowEvent(&qt_event);
}

bool effects_filter::key(key_event const& event)
{
    if (!effects || !static_cast<render::effects_handler_impl*>(effects)->hasKeyboardGrab()) {
        return false;
    }
    waylandServer()->seat()->setFocusedKeyboardSurface(nullptr);
    pass_to_wayland_server(event);
    auto qt_event = key_to_qt_event(event);
    static_cast<render::effects_handler_impl*>(effects)->grabbedKeyboardEvent(&qt_event);
    return true;
}

bool effects_filter::key_repeat(key_event const& event)
{
    if (!effects || !static_cast<render::effects_handler_impl*>(effects)->hasKeyboardGrab()) {
        return false;
    }
    auto qt_event = key_to_qt_event(event);
    static_cast<render::effects_handler_impl*>(effects)->grabbedKeyboardEvent(&qt_event);
    return true;
}

bool effects_filter::touch_down(touch_down_event const& event)
{
    if (!effects) {
        return false;
    }
    return static_cast<render::effects_handler_impl*>(effects)->touchDown(
        event.id, event.pos, event.base.time_msec);
}

bool effects_filter::touch_motion(touch_motion_event const& event)
{
    if (!effects) {
        return false;
    }
    return static_cast<render::effects_handler_impl*>(effects)->touchMotion(
        event.id, event.pos, event.base.time_msec);
}

bool effects_filter::touch_up(touch_up_event const& event)
{
    if (!effects) {
        return false;
    }
    return static_cast<render::effects_handler_impl*>(effects)->touchUp(event.id,
                                                                        event.base.time_msec);
}

}
