/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "helpers.h"

#include "base/wayland/server.h"
#include "input/event_filter.h"
#include "input/qt_event.h"
#include "input/redirect.h"
#include "main.h"
#include "render/compositor.h"
#include "render/effects.h"
#include "win/space.h"

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
        if (!this->redirect.space.render.effects) {
            return false;
        }
        auto qt_event = button_to_qt_event(*this->redirect.pointer(), event);
        return this->redirect.space.render.effects->checkInputWindowEvent(&qt_event);
    }

    bool motion(motion_event const& event) override
    {
        if (!this->redirect.space.render.effects) {
            return false;
        }
        auto qt_event = motion_to_qt_event(*this->redirect.pointer(), event);
        return this->redirect.space.render.effects->checkInputWindowEvent(&qt_event);
    }

    bool axis(axis_event const& event) override
    {
        if (!this->redirect.space.render.effects) {
            return false;
        }
        auto qt_event = axis_to_qt_event(*this->redirect.pointer(), event);
        return this->redirect.space.render.effects->checkInputWindowEvent(&qt_event);
    }

    bool key(key_event const& event) override
    {
        if (!this->redirect.space.render.effects
            || !this->redirect.space.render.effects->hasKeyboardGrab()) {
            return false;
        }
        waylandServer()->seat()->setFocusedKeyboardSurface(nullptr);
        pass_to_wayland_server(event);
        auto qt_event = key_to_qt_event(event);
        this->redirect.space.render.effects->grabbedKeyboardEvent(&qt_event);
        return true;
    }

    bool key_repeat(key_event const& event) override
    {
        if (!this->redirect.space.render.effects
            || !this->redirect.space.render.effects->hasKeyboardGrab()) {
            return false;
        }
        auto qt_event = key_to_qt_event(event);
        this->redirect.space.render.effects->grabbedKeyboardEvent(&qt_event);
        return true;
    }

    bool touch_down(touch_down_event const& event) override
    {
        if (!this->redirect.space.render.effects) {
            return false;
        }
        return this->redirect.space.render.effects->touchDown(
            event.id, event.pos, event.base.time_msec);
    }

    bool touch_motion(touch_motion_event const& event) override
    {
        if (!this->redirect.space.render.effects) {
            return false;
        }
        return this->redirect.space.render.effects->touchMotion(
            event.id, event.pos, event.base.time_msec);
    }

    bool touch_up(touch_up_event const& event) override
    {
        if (!this->redirect.space.render.effects) {
            return false;
        }
        return this->redirect.space.render.effects->touchUp(event.id, event.base.time_msec);
    }
};

}
