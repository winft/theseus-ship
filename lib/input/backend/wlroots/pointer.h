/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control/pointer.h"

#include "base/utils.h"
#include "config-kwin.h"
#include "input/pointer.h"

extern "C" {
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_pointer.h>
}

namespace KWin::input::backend::wlroots
{

template<typename Platform>
class pointer;

template<typename Platform>
void pointer_handle_destroy(struct wl_listener* listener, void* /*data*/)
{
    base::event_receiver<pointer<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    platform_remove_pointer(pointer, *pointer->platform);
    delete pointer;
}

template<typename Platform>
void handle_motion(struct wl_listener* listener, void* data)
{
    base::event_receiver<pointer<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_pointer_motion_event*>(data);

    auto event = motion_event{
        QPointF(wlr_event->delta_x, wlr_event->delta_y),
        QPointF(wlr_event->unaccel_dx, wlr_event->unaccel_dy),
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->motion(event);
}

template<typename Platform>
void handle_motion_absolute(struct wl_listener* listener, void* data)
{
    base::event_receiver<pointer<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_pointer_motion_absolute_event*>(data);

    auto event = motion_absolute_event{
        QPointF(wlr_event->x, wlr_event->y),
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->motion_absolute(event);
}

template<typename Platform>
void handle_button(struct wl_listener* listener, void* data)
{
    base::event_receiver<pointer<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_pointer_button_event*>(data);

    auto event = button_event{
        wlr_event->button,
        wlr_event->state == WLR_BUTTON_RELEASED ? button_state::released : button_state::pressed,
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->button_changed(event);
}

template<typename Platform>
void handle_axis(struct wl_listener* listener, void* data)
{
    base::event_receiver<pointer<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_pointer_axis_event*>(data);

    auto get_source = [](auto wlr_source) {
        switch (wlr_source) {
        case WLR_AXIS_SOURCE_WHEEL:
            return axis_source::wheel;
        case WLR_AXIS_SOURCE_FINGER:
            return axis_source::finger;
        case WLR_AXIS_SOURCE_CONTINUOUS:
            return axis_source::continuous;
        case WLR_AXIS_SOURCE_WHEEL_TILT:
            return axis_source::wheel_tilt;
        default:
            return axis_source::unknown;
        }
    };

    auto event = axis_event{
        get_source(wlr_event->source),
        static_cast<axis_orientation>(wlr_event->orientation),
        wlr_event->delta,
        wlr_event->delta_discrete,
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->axis_changed(event);
}

template<typename Platform>
void handle_swipe_begin(struct wl_listener* listener, void* data)
{
    base::event_receiver<pointer<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_pointer_swipe_begin_event*>(data);

    auto event = swipe_begin_event{
        wlr_event->fingers,
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->swipe_begin(event);
}

template<typename Platform>
void handle_swipe_update(struct wl_listener* listener, void* data)
{
    base::event_receiver<pointer<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_pointer_swipe_update_event*>(data);

    auto event = swipe_update_event{
        wlr_event->fingers,
        QPointF(wlr_event->dx, wlr_event->dy),
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->swipe_update(event);
}

template<typename Platform>
void handle_swipe_end(struct wl_listener* listener, void* data)
{
    base::event_receiver<pointer<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_pointer_swipe_end_event*>(data);

    auto event = swipe_end_event{
        wlr_event->cancelled,
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->swipe_end(event);
}

template<typename Platform>
void handle_pinch_begin(struct wl_listener* listener, void* data)
{
    base::event_receiver<pointer<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_pointer_pinch_begin_event*>(data);

    auto event = pinch_begin_event{
        wlr_event->fingers,
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->pinch_begin(event);
}

template<typename Platform>
void handle_pinch_update(struct wl_listener* listener, void* data)
{
    base::event_receiver<pointer<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_pointer_pinch_update_event*>(data);

    auto event = pinch_update_event{
        wlr_event->fingers,
        QPointF(wlr_event->dx, wlr_event->dy),
        wlr_event->scale,
        wlr_event->rotation,
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->pinch_update(event);
}

template<typename Platform>
void handle_pinch_end(struct wl_listener* listener, void* data)
{
    base::event_receiver<pointer<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_pointer_pinch_end_event*>(data);

    auto event = pinch_end_event{
        wlr_event->cancelled,
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->pinch_end(event);
}

template<typename Platform>
void handle_hold_begin(struct wl_listener* listener, void* data)
{
    base::event_receiver<pointer<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_pointer_hold_begin_event*>(data);

    auto event = hold_begin_event{
        wlr_event->fingers,
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->hold_begin(event);
}

template<typename Platform>
void handle_hold_end(struct wl_listener* listener, void* data)
{
    base::event_receiver<pointer<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_pointer_hold_end_event*>(data);

    auto event = hold_end_event{
        wlr_event->cancelled,
        {
            pointer,
            wlr_event->time_msec,
        },
    };

    Q_EMIT pointer->hold_end(event);
}

template<typename Platform>
void handle_frame(struct wl_listener* listener, void* /*data*/)
{
    base::event_receiver<pointer<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto pointer = event_receiver_struct->receiver;

    Q_EMIT pointer->frame();
}

template<typename Platform>
class pointer : public input::pointer
{
public:
    using er = base::event_receiver<pointer<Platform>>;

    pointer(wlr_input_device* dev, Platform* platform)
        : platform{platform}
    {
        auto backend = wlr_pointer_from_input_device(dev);

        if (auto libinput = get_libinput_device(dev)) {
            control = std::make_unique<pointer_control>(libinput, platform->config.main);
        }

        destroyed.receiver = this;
        destroyed.event.notify = pointer_handle_destroy<Platform>;
        wl_signal_add(&dev->events.destroy, &destroyed.event);

        motion_rec.receiver = this;
        motion_rec.event.notify = handle_motion<Platform>;
        wl_signal_add(&backend->events.motion, &motion_rec.event);

        motion_absolute_rec.receiver = this;
        motion_absolute_rec.event.notify = handle_motion_absolute<Platform>;
        wl_signal_add(&backend->events.motion_absolute, &motion_absolute_rec.event);

        button_rec.receiver = this;
        button_rec.event.notify = handle_button<Platform>;
        wl_signal_add(&backend->events.button, &button_rec.event);

        axis_rec.receiver = this;
        axis_rec.event.notify = handle_axis<Platform>;
        wl_signal_add(&backend->events.axis, &axis_rec.event);

        swipe_begin_rec.receiver = this;
        swipe_begin_rec.event.notify = handle_swipe_begin<Platform>;
        wl_signal_add(&backend->events.swipe_begin, &swipe_begin_rec.event);

        swipe_update_rec.receiver = this;
        swipe_update_rec.event.notify = handle_swipe_update<Platform>;
        wl_signal_add(&backend->events.swipe_update, &swipe_update_rec.event);

        swipe_end_rec.receiver = this;
        swipe_end_rec.event.notify = handle_swipe_end<Platform>;
        wl_signal_add(&backend->events.swipe_end, &swipe_end_rec.event);

        pinch_begin_rec.receiver = this;
        pinch_begin_rec.event.notify = handle_pinch_begin<Platform>;
        wl_signal_add(&backend->events.pinch_begin, &pinch_begin_rec.event);

        pinch_update_rec.receiver = this;
        pinch_update_rec.event.notify = handle_pinch_update<Platform>;
        wl_signal_add(&backend->events.pinch_update, &pinch_update_rec.event);

        pinch_end_rec.receiver = this;
        pinch_end_rec.event.notify = handle_pinch_end<Platform>;
        wl_signal_add(&backend->events.pinch_end, &pinch_end_rec.event);

        hold_begin_rec.receiver = this;
        hold_begin_rec.event.notify = handle_hold_begin<Platform>;
        wl_signal_add(&backend->events.hold_begin, &hold_begin_rec.event);

        hold_end_rec.receiver = this;
        hold_end_rec.event.notify = handle_hold_end<Platform>;
        wl_signal_add(&backend->events.hold_end, &hold_end_rec.event);

        frame_rec.receiver = this;
        frame_rec.event.notify = handle_frame<Platform>;
        wl_signal_add(&backend->events.frame, &frame_rec.event);
    }

    pointer(pointer const&) = delete;
    pointer& operator=(pointer const&) = delete;
    ~pointer() override = default;

    Platform* platform;

private:
    er destroyed;
    er motion_rec;
    er motion_absolute_rec;
    er button_rec;
    er axis_rec;
    er frame_rec;
    er swipe_begin_rec;
    er swipe_update_rec;
    er swipe_end_rec;
    er pinch_begin_rec;
    er pinch_update_rec;
    er pinch_end_rec;
    er hold_begin_rec;
    er hold_end_rec;
};

}
