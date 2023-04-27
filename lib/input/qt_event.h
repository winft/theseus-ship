/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "event.h"
#include "keyboard.h"
#include "xkb/helpers.h"
#include "xkb/keyboard.h"

#include "kwin_export.h"

#include <QEvent>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>
#include <linux/input.h>

namespace KWin::input
{

QHash<uint32_t, Qt::MouseButton> const button_map = {
    {BTN_LEFT, Qt::LeftButton},
    {BTN_MIDDLE, Qt::MiddleButton},
    {BTN_RIGHT, Qt::RightButton},
    // in QtWayland mapped like that
    {BTN_SIDE, Qt::ExtraButton1},
    {BTN_EXTRA, Qt::ExtraButton2},
    {BTN_FORWARD, Qt::ExtraButton3},
    {BTN_BACK, Qt::ExtraButton4},
    {BTN_TASK, Qt::ExtraButton5},
    {0x118, Qt::ExtraButton6},
    {0x119, Qt::ExtraButton7},
    {0x11a, Qt::ExtraButton8},
    {0x11b, Qt::ExtraButton9},
    {0x11c, Qt::ExtraButton10},
    {0x11d, Qt::ExtraButton11},
    {0x11e, Qt::ExtraButton12},
    {0x11f, Qt::ExtraButton13},
};

template<typename Ptr>
QMouseEvent
get_qt_mouse_event(Ptr const& ptr, QEvent::Type type, QPointF const& pos, Qt::MouseButton button)
{
    auto buttons = ptr.buttons();
    auto modifiers = xkb::get_active_keyboard_modifiers(ptr.redirect->platform);

    return QMouseEvent(type, pos, pos, button, buttons, modifiers);
}

inline Qt::MouseButton button_to_qt_mouse_button(uint32_t button)
{
    // all other values get mapped to ExtraButton24
    // this is actually incorrect but doesn't matter in our usage
    // KWin internally doesn't use these high extra buttons anyway
    // it's only needed for recognizing whether buttons are pressed
    // if multiple buttons are mapped to the value the evaluation whether
    // buttons are pressed is correct and that's all we care about.
    return button_map.value(button, Qt::ExtraButton24);
}

inline uint32_t qt_mouse_button_to_button(Qt::MouseButton button)
{
    return button_map.key(button);
}

inline Qt::Key key_to_qt_key(uint32_t key, xkb::keyboard* xkb)
{
    auto const global_shortcuts_mods = xkb->modifiers_relevant_for_global_shortcuts(key);

    return xkb->to_qt_key(
        xkb->keysym, key, global_shortcuts_mods ? Qt::ControlModifier : Qt::KeyboardModifiers());
}

template<typename Ptr>
QMouseEvent get_qt_mouse_button_event(Ptr const& ptr, uint32_t key, button_state state)
{
    auto type = state == button_state::pressed ? QMouseEvent::MouseButtonPress
                                               : QMouseEvent::MouseButtonRelease;
    auto pos = ptr.pos();
    auto button = button_to_qt_mouse_button(key);

    return get_qt_mouse_event(ptr, type, pos, button);
}

template<typename Ptr>
QMouseEvent get_qt_mouse_motion_absolute_event(Ptr const& ptr, QPointF const& pos)
{
    return get_qt_mouse_event(ptr, QMouseEvent::MouseMove, pos, Qt::NoButton);
}

template<typename Ptr>
QMouseEvent button_to_qt_event(Ptr const& ptr, button_event const& event)
{
    return get_qt_mouse_button_event(ptr, event.key, event.state);
}

// TODO(romangg): This function is bad, because the consumer still has to set the timestamp. It's
//                not possible differnently, because QMouseEvent can't be moved/copied. Replace the
//                class somehow.
template<typename Ptr>
QMouseEvent motion_to_qt_event(Ptr const& ptr, motion_event const& /*event*/)
{
    return get_qt_mouse_event(ptr, QMouseEvent::MouseMove, ptr.pos(), Qt::NoButton);
}

// TODO(romangg): This function is bad, because the consumer still has to set the timestamp. It's
//                not possible differnently, because QWheelEvent can't be moved/copied. Replace the
//                class somehow.
template<typename Ptr>
QWheelEvent axis_to_qt_event(Ptr const& ptr, axis_event const& event)
{
    auto pos = ptr.pos();
    auto buttons = ptr.buttons();

    // TODO(romangg): In the future only get modifiers from keyboards associated with the seat of
    //                the pointer the event originated from.
    auto mods = xkb::get_active_keyboard_modifiers(ptr.redirect->platform);

    auto const delta_point = event.orientation == axis_orientation::horizontal
        ? QPoint(event.delta, 0)
        : QPoint(0, event.delta);

    return {pos, pos, QPoint(), delta_point, buttons, mods, Qt::NoScrollPhase, false};
}

inline QKeyEvent key_to_qt_event(key_event const& event)
{
    auto type = event.state == key_state::pressed ? QEvent::KeyPress : QEvent::KeyRelease;
    auto const& xkb = event.base.dev->xkb;
    auto mods = xkb->qt_modifiers;

    auto const globalShortcutsModifiers
        = xkb->modifiers_relevant_for_global_shortcuts(event.keycode);
    auto const key
        = xkb->to_qt_key(xkb->keysym,
                         event.keycode,
                         globalShortcutsModifiers ? Qt::ControlModifier : Qt::KeyboardModifiers());
    return {type,
            key,
            mods,
            event.keycode,
            xkb->keysym,
            0,
            QString::fromStdString(xkb->to_string(xkb->keysym)),
            false};
}

}
