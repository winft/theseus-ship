/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "helpers.h"

#include "base/wayland/server.h"
#include "input/event.h"
#include "input/event_filter.h"
#include "input/keyboard_redirect.h"
#include "input/pointer_redirect.h"
#include "input/qt_event.h"
#include "input/redirect.h"
#include "input/touch_redirect.h"
#include "main.h"

#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/touch_pool.h>

namespace KWin::input
{

/**
 * The remaining default input filter which forwards events to other windows
 */
template<typename Redirect>
class forward_filter : public event_filter<Redirect>
{
public:
    explicit forward_filter(Redirect& redirect)
        : event_filter<Redirect>(redirect)
    {
    }

    bool key(key_event const& event) override
    {
        auto seat = waylandServer()->seat();
        this->redirect.keyboard->update();
        seat->setTimestamp(event.base.time_msec);
        pass_to_wayland_server(event);
        return true;
    }

    bool button(button_event const& event) override
    {
        auto seat = waylandServer()->seat();
        seat->setTimestamp(event.base.time_msec);

        switch (event.state) {
        case button_state::pressed:
            seat->pointers().button_pressed(event.key);
            break;
        case button_state::released:
            seat->pointers().button_released(event.key);
            break;
        }

        return true;
    }

    bool motion(motion_event const& event) override
    {
        auto seat = waylandServer()->seat();
        seat->setTimestamp(event.base.time_msec);

        seat->pointers().set_position(this->redirect.pointer->pos());
        if (!event.delta.isNull()) {
            seat->pointers().relative_motion(
                QSizeF(event.delta.x(), event.delta.y()),
                QSizeF(event.unaccel_delta.x(), event.unaccel_delta.y()),
                event.base.time_msec);
        }

        return true;
    }

    bool touch_down(touch_down_event const& event) override
    {
        auto seat = waylandServer()->seat();
        seat->setTimestamp(event.base.time_msec);
        this->redirect.touch->insertId(event.id, seat->touches().touch_down(event.pos));
        return true;
    }

    bool touch_motion(touch_motion_event const& event) override
    {
        auto seat = waylandServer()->seat();
        seat->setTimestamp(event.base.time_msec);
        const qint32 wraplandId = this->redirect.touch->mappedId(event.id);
        if (wraplandId != -1) {
            seat->touches().touch_move(wraplandId, event.pos);
        }
        return true;
    }

    bool touch_up(touch_up_event const& event) override
    {
        auto seat = waylandServer()->seat();
        seat->setTimestamp(event.base.time_msec);
        const qint32 wraplandId = this->redirect.touch->mappedId(event.id);
        if (wraplandId != -1) {
            seat->touches().touch_up(wraplandId);
            this->redirect.touch->removeId(event.id);
        }
        return true;
    }

    bool axis(axis_event const& event) override
    {
        auto seat = waylandServer()->seat();
        seat->setTimestamp(event.base.time_msec);

        using wrap_source = Wrapland::Server::PointerAxisSource;

        auto source = wrap_source::Unknown;
        switch (event.source) {
        case axis_source::wheel:
            source = wrap_source::Wheel;
            break;
        case axis_source::finger:
            source = wrap_source::Finger;
            break;
        case axis_source::continuous:
            source = wrap_source::Continuous;
            break;
        case axis_source::wheel_tilt:
            source = wrap_source::WheelTilt;
            break;
        case axis_source::unknown:
        default:
            source = wrap_source::Unknown;
            break;
        }

        auto orientation = (event.orientation == axis_orientation::horizontal)
            ? Qt::Orientation::Horizontal
            : Qt::Orientation::Vertical;

        seat->pointers().send_axis(orientation, event.delta, event.delta_discrete, source);
        return true;
    }

    bool pinch_begin(pinch_begin_event const& event) override
    {
        auto seat = waylandServer()->seat();
        seat->setTimestamp(event.base.time_msec);
        seat->pointers().start_pinch_gesture(event.fingers);

        return true;
    }

    bool pinch_update(pinch_update_event const& event) override
    {
        auto seat = waylandServer()->seat();
        seat->setTimestamp(event.base.time_msec);
        seat->pointers().update_pinch_gesture(
            QSize(event.delta.x(), event.delta.y()), event.scale, event.rotation);

        return true;
    }

    bool pinch_end(pinch_end_event const& event) override
    {
        auto seat = waylandServer()->seat();
        seat->setTimestamp(event.base.time_msec);

        if (event.cancelled) {
            seat->pointers().cancel_pinch_gesture();
        } else {
            seat->pointers().end_pinch_gesture();
        }

        return true;
    }

    bool swipe_begin(swipe_begin_event const& event) override
    {
        auto seat = waylandServer()->seat();
        seat->setTimestamp(event.base.time_msec);
        seat->pointers().start_swipe_gesture(event.fingers);

        return true;
    }

    bool swipe_update(swipe_update_event const& event) override
    {
        auto seat = waylandServer()->seat();
        seat->setTimestamp(event.base.time_msec);
        seat->pointers().update_swipe_gesture(QSize(event.delta.x(), event.delta.y()));

        return true;
    }

    bool swipe_end(swipe_end_event const& event) override
    {
        auto seat = waylandServer()->seat();
        seat->setTimestamp(event.base.time_msec);

        if (event.cancelled) {
            seat->pointers().cancel_swipe_gesture();
        } else {
            seat->pointers().end_swipe_gesture();
        }

        return true;
    }
};

}
