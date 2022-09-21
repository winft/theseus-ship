/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event.h"
#include "input/event_filter.h"
#include "input/keyboard.h"
#include "input/pointer_redirect.h"
#include "input/qt_event.h"
#include "input/xkb/helpers.h"
#include "win/input.h"
#include "win/move.h"

namespace KWin::input
{

template<typename Redirect>
class move_resize_filter : public event_filter<Redirect>
{
public:
    explicit move_resize_filter(Redirect& redirect)
        : event_filter<Redirect>(redirect)
    {
    }

    bool button(button_event const& /*event*/) override
    {
        auto window = this->redirect.space.move_resize_window;
        if (!window) {
            return false;
        }
        if (this->redirect.pointer->buttons() == Qt::NoButton) {
            std::visit(overload{[&](auto&& win) { win::end_move_resize(win); }}, *window);
        }
        return true;
    }

    bool motion(motion_event const& /*event*/) override
    {
        auto window = this->redirect.space.move_resize_window;
        if (!window) {
            return false;
        }

        auto pos = this->redirect.globalPointer();
        std::visit(overload{[&](auto&& win) { win::update_move_resize(win, pos.toPoint()); }},
                   *window);

        return true;
    }

    bool axis(axis_event const& /*event*/) override
    {
        return this->redirect.space.move_resize_window.has_value();
    }

    void process_key_press(typename Redirect::window_t window, key_event const& event)
    {
        std::visit(overload{[&](auto&& win) {
                       win::key_press_event(
                           win,
                           key_to_qt_key(event.keycode, event.base.dev->xkb.get())
                               | xkb::get_active_keyboard_modifiers(this->redirect.platform));

                       if (win::is_move(win) || win::is_resize(win)) {
                           // Only update if mode didn't end.
                           win::update_move_resize(win, this->redirect.globalPointer());
                       }
                   }},
                   window);
    }

    bool key(key_event const& event) override
    {
        auto window = this->redirect.space.move_resize_window;
        if (!window) {
            return false;
        }

        if (event.state == key_state::pressed) {
            process_key_press(*window, event);
        }
        return true;
    }

    bool key_repeat(key_event const& event) override
    {
        auto window = this->redirect.space.move_resize_window;
        if (!window) {
            return false;
        }

        process_key_press(*window, event);
        return true;
    }

    bool touch_down(touch_down_event const& /*event*/) override
    {
        auto c = this->redirect.space.move_resize_window;
        if (!c) {
            return false;
        }
        return true;
    }

    bool touch_motion(touch_motion_event const& event) override
    {
        Q_UNUSED(time)
        auto c = this->redirect.space.move_resize_window;
        if (!c) {
            return false;
        }
        if (!m_set) {
            m_id = event.id;
            m_set = true;
        }
        if (m_id == event.id) {
            std::visit(
                overload{[&](auto&& win) { win::update_move_resize(win, event.pos.toPoint()); }},
                *c);
        }
        return true;
    }

    bool touch_up(touch_up_event const& event) override
    {
        auto c = this->redirect.space.move_resize_window;
        if (!c) {
            return false;
        }
        if (m_id == event.id || !m_set) {
            std::visit(overload{[](auto&& win) { win::end_move_resize(win); }}, *c);
            m_set = false;
            // pass through to update decoration filter later on
            return false;
        }
        m_set = false;
        return true;
    }

private:
    qint32 m_id = 0;
    bool m_set = false;
};

}
