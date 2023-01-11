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
#include "input/xkb/helpers.h"
#include "main.h"

#include <Wrapland/Server/seat.h>

namespace KWin::input
{

template<typename Redirect>
class tabbox_filter : public event_filter<Redirect>
{
public:
    explicit tabbox_filter(Redirect& redirect)
        : event_filter<Redirect>(redirect)
    {
    }

    bool button(button_event const& event) override
    {
        auto& tabbox = this->redirect.space.tabbox;
        if (!tabbox || !tabbox->is_grabbed()) {
            return false;
        }

        auto qt_event = button_to_qt_event(*this->redirect.pointer, event);
        return tabbox->handle_mouse_event(&qt_event);
    }

    bool motion(motion_event const& event) override
    {
        auto& tabbox = this->redirect.space.tabbox;

        if (!tabbox || !tabbox->is_grabbed()) {
            return false;
        }

        auto qt_event = motion_to_qt_event(*this->redirect.pointer, event);
        return tabbox->handle_mouse_event(&qt_event);
    }

    bool key(key_event const& event) override
    {
        auto& tabbox = this->redirect.space.tabbox;

        if (!tabbox || !tabbox->is_grabbed()) {
            return false;
        }

        auto seat = waylandServer()->seat();
        seat->setFocusedKeyboardSurface(nullptr);
        this->redirect.pointer->setEnableConstraints(false);

        // pass the key event to the seat, so that it has a proper model of the currently hold keys
        // this is important for combinations like alt+shift to ensure that shift is not considered
        // pressed
        pass_to_wayland_server(this->redirect, event);

        if (event.state == key_state::pressed) {
            auto mods = xkb::get_active_keyboard_modifiers(this->redirect.platform);
            tabbox->key_press(mods | key_to_qt_key(event.keycode, event.base.dev->xkb.get()));
        } else if (xkb::get_active_keyboard_modifiers_relevant_for_global_shortcuts(
                       this->redirect.platform)
                   == Qt::NoModifier) {
            tabbox->modifiers_released();
        }
        return true;
    }

    bool key_repeat(key_event const& event) override
    {
        auto& tabbox = this->redirect.space.tabbox;

        if (!tabbox || !tabbox->is_grabbed()) {
            return false;
        }

        auto mods = xkb::get_active_keyboard_modifiers(this->redirect.platform);
        tabbox->key_press(mods | key_to_qt_key(event.keycode, event.base.dev->xkb.get()));
        return true;
    }

    bool axis(axis_event const& event) override
    {
        auto& tabbox = this->redirect.space.tabbox;

        if (!tabbox || !tabbox->is_grabbed()) {
            return false;
        }

        auto qt_event = axis_to_qt_event(*this->redirect.pointer, event);
        return tabbox->handle_wheel_event(&qt_event);
    }
};

}
