/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "helpers.h"

#include "base/wayland/server.h"
#include "input/event_filter.h"
#include "input/qt_event.h"

#include <Wrapland/Server/seat.h>

namespace KWin::input
{

template<typename Redirect>
class effects_filter : public event_filter<Redirect>
{
public:
    explicit effects_filter(Redirect& redirect)
        : event_filter<Redirect>(redirect)
    {
    }

    bool button(button_event const& event) override
    {
        if (!get_effects()) {
            return false;
        }
        auto qt_event = button_to_qt_event(*this->redirect.pointer, event);
        return get_effects()->checkInputWindowEvent(&qt_event);
    }

    bool motion(motion_event const& event) override
    {
        if (!get_effects()) {
            return false;
        }
        auto qt_event = motion_to_qt_event(*this->redirect.pointer, event);
        qt_event.setTimestamp(event.base.time_msec);
        return get_effects()->checkInputWindowEvent(&qt_event);
    }

    bool axis(axis_event const& event) override
    {
        if (!get_effects()) {
            return false;
        }
        auto qt_event = axis_to_qt_event(*this->redirect.pointer, event);
        qt_event.setTimestamp(event.base.time_msec);
        return get_effects()->checkInputWindowEvent(&qt_event);
    }

    bool key(key_event const& event) override
    {
        if (!get_effects() || !get_effects()->hasKeyboardGrab()) {
            return false;
        }
        this->redirect.platform.base.server->seat()->setFocusedKeyboardSurface(nullptr);
        pass_to_wayland_server(this->redirect, event);
        auto qt_event = key_to_qt_event(event);
        get_effects()->grabbedKeyboardEvent(&qt_event);
        return true;
    }

    bool key_repeat(key_event const& event) override
    {
        if (!get_effects() || !get_effects()->hasKeyboardGrab()) {
            return false;
        }
        auto qt_event = key_to_qt_event(event);
        get_effects()->grabbedKeyboardEvent(&qt_event);
        return true;
    }

    bool touch_down(touch_down_event const& event) override
    {
        if (!get_effects()) {
            return false;
        }
        return get_effects()->touchDown(event.id, event.pos, event.base.time_msec);
    }

    bool touch_motion(touch_motion_event const& event) override
    {
        if (!get_effects()) {
            return false;
        }
        return get_effects()->touchMotion(event.id, event.pos, event.base.time_msec);
    }

    bool touch_up(touch_up_event const& event) override
    {
        if (!get_effects()) {
            return false;
        }
        return get_effects()->touchUp(event.id, event.base.time_msec);
    }

private:
    auto& get_effects()
    {
        return this->redirect.platform.base.render->compositor->effects;
    }
};

}
