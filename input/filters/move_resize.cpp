/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "move_resize.h"

#include "input/event.h"
#include "input/keyboard.h"
#include "input/pointer_redirect.h"
#include "input/qt_event.h"
#include "input/redirect.h"
#include "input/xkb/helpers.h"
#include "main.h"
#include "win/input.h"
#include "win/move.h"
#include "win/space.h"

namespace KWin::input
{

move_resize_filter::move_resize_filter(input::redirect& redirect)
    : redirect{redirect}
{
}

bool move_resize_filter::button([[maybe_unused]] button_event const& event)
{
    auto window = redirect.space.move_resize_window;
    if (!window) {
        return false;
    }
    if (kwinApp()->input->redirect->pointer()->buttons() == Qt::NoButton) {
        win::end_move_resize(window);
    }
    return true;
}

bool move_resize_filter::motion([[maybe_unused]] motion_event const& event)
{
    auto window = redirect.space.move_resize_window;
    if (!window) {
        return false;
    }
    auto pos = kwinApp()->input->redirect->globalPointer();
    win::update_move_resize(window, pos.toPoint());
    return true;
}

bool move_resize_filter::axis([[maybe_unused]] axis_event const& event)
{
    return redirect.space.move_resize_window != nullptr;
}

void process_key_press(Toplevel* window, key_event const& event)
{
    auto const& input = kwinApp()->input;

    win::key_press_event(window,
                         key_to_qt_key(event.keycode, event.base.dev->xkb.get())
                             | xkb::get_active_keyboard_modifiers(input));

    if (win::is_move(window) || win::is_resize(window)) {
        // Only update if mode didn't end.
        win::update_move_resize(window, input->redirect->globalPointer());
    }
}

bool move_resize_filter::key(key_event const& event)
{
    auto window = redirect.space.move_resize_window;
    if (!window) {
        return false;
    }

    if (event.state == key_state::pressed) {
        process_key_press(window, event);
    }
    return true;
}

bool move_resize_filter::key_repeat(key_event const& event)
{
    auto window = redirect.space.move_resize_window;
    if (!window) {
        return false;
    }

    process_key_press(window, event);
    return true;
}

bool move_resize_filter::touch_down(touch_down_event const& /*event*/)
{
    auto c = redirect.space.move_resize_window;
    if (!c) {
        return false;
    }
    return true;
}

bool move_resize_filter::touch_motion(touch_motion_event const& event)
{
    Q_UNUSED(time)
    auto c = redirect.space.move_resize_window;
    if (!c) {
        return false;
    }
    if (!m_set) {
        m_id = event.id;
        m_set = true;
    }
    if (m_id == event.id) {
        win::update_move_resize(c, event.pos.toPoint());
    }
    return true;
}

bool move_resize_filter::touch_up(touch_up_event const& event)
{
    auto c = redirect.space.move_resize_window;
    if (!c) {
        return false;
    }
    if (m_id == event.id || !m_set) {
        win::end_move_resize(c);
        m_set = false;
        // pass through to update decoration filter later on
        return false;
    }
    m_set = false;
    return true;
}

}
