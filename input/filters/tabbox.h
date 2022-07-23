/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "helpers.h"

#include "base/wayland/server.h"
#include "input/event.h"
#include "input/event_filter.h"
#include "input/pointer_redirect.h"
#include "input/qt_event.h"
#include "input/redirect.h"
#include "input/xkb/helpers.h"
#include "main.h"
#include "win/space.h"
#include "win/tabbox/tabbox.h"

#include <Wrapland/Server/seat.h>

namespace KWin::input
{

class tabbox_filter : public event_filter
{
public:
    explicit tabbox_filter(input::redirect& redirect)
        : redirect{redirect}
    {
    }

    bool button(button_event const& event) override
    {
        auto& tabbox = redirect.space.tabbox;
        if (!tabbox || !tabbox->is_grabbed()) {
            return false;
        }

        auto qt_event = button_to_qt_event(event);
        return tabbox->handle_mouse_event(&qt_event);
    }

    bool motion(motion_event const& event) override
    {
        auto& tabbox = redirect.space.tabbox;

        if (!tabbox || !tabbox->is_grabbed()) {
            return false;
        }

        auto qt_event = motion_to_qt_event(event);
        return tabbox->handle_mouse_event(&qt_event);
    }

    bool key(key_event const& event) override
    {
        auto& tabbox = redirect.space.tabbox;

        if (!tabbox || !tabbox->is_grabbed()) {
            return false;
        }

        auto seat = waylandServer()->seat();
        seat->setFocusedKeyboardSurface(nullptr);
        kwinApp()->input->redirect->pointer()->setEnableConstraints(false);

        // pass the key event to the seat, so that it has a proper model of the currently hold keys
        // this is important for combinations like alt+shift to ensure that shift is not considered
        // pressed
        pass_to_wayland_server(event);

        if (event.state == key_state::pressed) {
            auto mods = xkb::get_active_keyboard_modifiers(kwinApp()->input);
            tabbox->key_press(mods | key_to_qt_key(event.keycode, event.base.dev->xkb.get()));
        } else if (xkb::get_active_keyboard_modifiers_relevant_for_global_shortcuts(
                       kwinApp()->input)
                   == Qt::NoModifier) {
            tabbox->modifiers_released();
        }
        return true;
    }

    bool key_repeat(key_event const& event) override
    {
        auto& tabbox = redirect.space.tabbox;

        if (!tabbox || !tabbox->is_grabbed()) {
            return false;
        }

        auto mods = xkb::get_active_keyboard_modifiers(kwinApp()->input);
        tabbox->key_press(mods | key_to_qt_key(event.keycode, event.base.dev->xkb.get()));
        return true;
    }

    bool axis(axis_event const& event) override
    {
        auto& tabbox = redirect.space.tabbox;

        if (!tabbox || !tabbox->is_grabbed()) {
            return false;
        }

        auto qt_event = axis_to_qt_event(event);
        return tabbox->handle_wheel_event(&qt_event);
    }

private:
    input::redirect& redirect;
};

}
