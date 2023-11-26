/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control/touch.h"
#include <input/backend/wlroots/device_helpers.h>

#include "base/utils.h"
#include "config-kwin.h"
#include "input/platform.h"
#include "input/touch.h"

extern "C" {
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_touch.h>
}

namespace KWin::input::backend::wlroots
{

template<typename Platform>
class touch;

template<typename Platform>
void touch_handle_destroy(struct wl_listener* listener, void* /*data*/)
{
    base::event_receiver<touch<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto touch = event_receiver_struct->receiver;
    if (touch->platform) {
        platform_remove_touch(touch, *touch->platform->frontend);
    }
    delete touch;
}

template<typename Platform>
void handle_down(struct wl_listener* listener, void* data)
{
    base::event_receiver<touch<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto touch = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_touch_down_event*>(data);

    auto event = touch_down_event{
        wlr_event->touch_id,
        QPointF(wlr_event->x, wlr_event->y),
        {
            touch,
            wlr_event->time_msec,
        },
    };

    Q_EMIT touch->qobject->down(event);
}

template<typename Platform>
void handle_up(struct wl_listener* listener, void* data)
{
    base::event_receiver<touch<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto touch = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_touch_up_event*>(data);

    auto event = touch_up_event{
        wlr_event->touch_id,
        {
            touch,
            wlr_event->time_msec,
        },
    };

    Q_EMIT touch->qobject->up(event);
}

template<typename Platform>
void touch_handle_motion(struct wl_listener* listener, void* data)
{
    base::event_receiver<touch<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto touch = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_touch_motion_event*>(data);

    auto event = touch_motion_event{
        wlr_event->touch_id,
        QPointF(wlr_event->x, wlr_event->y),
        {
            touch,
            wlr_event->time_msec,
        },
    };

    Q_EMIT touch->qobject->motion(event);
}

template<typename Platform>
void handle_cancel(struct wl_listener* listener, void* data)
{
    base::event_receiver<touch<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto touch = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_touch_cancel_event*>(data);

    auto event = touch_cancel_event{
        wlr_event->touch_id,
        {
            touch,
            wlr_event->time_msec,
        },
    };

    Q_EMIT touch->qobject->cancel(event);
}

template<typename Platform>
void touch_handle_frame(struct wl_listener* listener, void* /*data*/)
{
    base::event_receiver<touch<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto touch = event_receiver_struct->receiver;

    Q_EMIT touch->qobject->frame();
}

template<typename Platform>
class touch : public input::touch_impl<typename Platform::frontend_type::base_t>
{
public:
    using er = base::event_receiver<touch<Platform>>;

    touch(wlr_input_device* dev, Platform* platform)
        : touch_impl<typename Platform::frontend_type::base_t>(platform->frontend->base)
        , platform{platform}
    {
        auto backend = wlr_touch_from_input_device(dev);

        if (auto libinput = get_libinput_device(dev)) {
            this->control
                = std::make_unique<touch_control>(libinput, platform->frontend->config.main);
        }
        this->output = this->get_output();

        destroyed.receiver = this;
        destroyed.event.notify = touch_handle_destroy<Platform>;
        wl_signal_add(&dev->events.destroy, &destroyed.event);

        down_rec.receiver = this;
        down_rec.event.notify = handle_down<Platform>;
        wl_signal_add(&backend->events.down, &down_rec.event);

        up_rec.receiver = this;
        up_rec.event.notify = handle_up<Platform>;
        wl_signal_add(&backend->events.up, &up_rec.event);

        motion_rec.receiver = this;
        motion_rec.event.notify = touch_handle_motion<Platform>;
        wl_signal_add(&backend->events.motion, &motion_rec.event);

        cancel_rec.receiver = this;
        cancel_rec.event.notify = handle_cancel<Platform>;
        wl_signal_add(&backend->events.cancel, &cancel_rec.event);

        frame_rec.receiver = this;
        frame_rec.event.notify = touch_handle_frame<Platform>;
        wl_signal_add(&backend->events.frame, &frame_rec.event);
    }

    Platform* platform;

private:
    er destroyed;
    er down_rec;
    er up_rec;
    er motion_rec;
    er cancel_rec;
    er frame_rec;
};

}
