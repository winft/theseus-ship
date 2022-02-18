/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "qt_event.h"

#include "keyboard.h"
#include "keyboard_redirect.h"
#include "pointer_redirect.h"
#include "redirect.h"
#include "xkb/helpers.h"

#include "main.h"

#include <QHash>
#include <cmath>
#include <linux/input.h>
#include <qevent.h>

namespace KWin::input
{

QHash<uint32_t, Qt::MouseButton> const button_map = {
    {BTN_LEFT, Qt::LeftButton},
    {BTN_MIDDLE, Qt::MiddleButton},
    {BTN_RIGHT, Qt::RightButton},
    // in QtWayland mapped like that
    {BTN_SIDE, Qt::ExtraButton1},
    // in QtWayland mapped like that
    {BTN_EXTRA, Qt::ExtraButton2},
    {BTN_BACK, Qt::BackButton},
    {BTN_FORWARD, Qt::ForwardButton},
    {BTN_TASK, Qt::TaskButton},
    // mapped like that in QtWayland
    {0x118, Qt::ExtraButton6},
    {0x119, Qt::ExtraButton7},
    {0x11a, Qt::ExtraButton8},
    {0x11b, Qt::ExtraButton9},
    {0x11c, Qt::ExtraButton10},
    {0x11d, Qt::ExtraButton11},
    {0x11e, Qt::ExtraButton12},
    {0x11f, Qt::ExtraButton13},
};

Qt::MouseButton button_to_qt_mouse_button(uint32_t button)
{
    // all other values get mapped to ExtraButton24
    // this is actually incorrect but doesn't matter in our usage
    // KWin internally doesn't use these high extra buttons anyway
    // it's only needed for recognizing whether buttons are pressed
    // if multiple buttons are mapped to the value the evaluation whether
    // buttons are pressed is correct and that's all we care about.
    return button_map.value(button, Qt::ExtraButton24);
}

uint32_t qt_mouse_button_to_button(Qt::MouseButton button)
{
    return button_map.key(button);
}

Qt::Key key_to_qt_key(uint32_t key, xkb::keyboard* xkb)
{
    auto const global_shortcuts_mods = xkb->modifiers_relevant_for_global_shortcuts(key);

    return xkb->to_qt_key(
        xkb->keysym, key, global_shortcuts_mods ? Qt::ControlModifier : Qt::KeyboardModifiers());
}

QMouseEvent get_qt_mouse_event(QEvent::Type type, QPointF const& pos, Qt::MouseButton button)
{
    auto buttons = kwinApp()->input->redirect->pointer()->buttons();
    auto modifiers = xkb::get_active_keyboard_modifiers(kwinApp()->input.get());

    return QMouseEvent(type, pos, pos, button, buttons, modifiers);
}

QMouseEvent get_qt_mouse_button_event(uint32_t key, button_state state)
{
    auto type = state == button_state::pressed ? QMouseEvent::MouseButtonPress
                                               : QMouseEvent::MouseButtonRelease;
    auto pos = kwinApp()->input->redirect->pointer()->pos();
    auto button = button_to_qt_mouse_button(key);

    return get_qt_mouse_event(type, pos, button);
}

QMouseEvent get_qt_mouse_motion_absolute_event(QPointF const& pos)
{
    return get_qt_mouse_event(QMouseEvent::MouseMove, pos, Qt::NoButton);
}

QMouseEvent button_to_qt_event(button_event const& event)
{
    return get_qt_mouse_button_event(event.key, event.state);
}

QMouseEvent motion_to_qt_event([[maybe_unused]] motion_event const& event)
{
    auto pos = kwinApp()->input->redirect->pointer()->pos();

    auto qt_event = get_qt_mouse_event(QMouseEvent::MouseMove, pos, Qt::NoButton);
    qt_event.setTimestamp(event.base.time_msec);

    return qt_event;
}

QMouseEvent motion_absolute_to_qt_event(motion_absolute_event const& event)
{
    auto qt_event = get_qt_mouse_motion_absolute_event(event.pos);
    qt_event.setTimestamp(event.base.time_msec);

    return qt_event;
}

QWheelEvent axis_to_qt_event(axis_event const& event)
{
    auto pos = kwinApp()->input->redirect->pointer()->pos();
    auto buttons = kwinApp()->input->redirect->pointer()->buttons();

    // TODO(romangg): In the future only get modifiers from keyboards associated with the seat of
    //                the pointer the event originated from.
    auto mods = xkb::get_active_keyboard_modifiers(kwinApp()->input.get());

    auto const delta_int = static_cast<int>(std::round(event.delta));
    auto delta_point = QPoint(event.delta, 0);
    auto orientation = Qt::Horizontal;

    if (event.orientation == axis_orientation::vertical) {
        delta_point = QPoint(0, event.delta);
        orientation = Qt::Vertical;
    }

    auto qt_event
        = QWheelEvent(pos, pos, QPoint(), delta_point, delta_int, orientation, buttons, mods);
    qt_event.setTimestamp(event.base.time_msec);

    return qt_event;
}

QKeyEvent key_to_qt_event(key_event const& event)
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
