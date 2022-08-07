/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "touch.h"

#include "config-kwin.h"
#include "control/touch.h"
#include "platform.h"

extern "C" {
#include <wlr/backend/libinput.h>
}

namespace KWin::input::backend::wlroots
{

using er = base::event_receiver<touch>;

static void handle_destroy(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto touch = event_receiver_struct->receiver;
    delete touch;
}

static void handle_down(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto touch = event_receiver_struct->receiver;
#if HAVE_WLR_BASE_INPUT_DEVICES
    auto wlr_event = reinterpret_cast<wlr_touch_down_event*>(data);
#else
    auto wlr_event = reinterpret_cast<wlr_event_touch_down*>(data);
#endif

    auto event = touch_down_event{
        wlr_event->touch_id,
        QPointF(wlr_event->x, wlr_event->y),
        {
            touch,
            wlr_event->time_msec,
        },
    };

    Q_EMIT touch->down(event);
}

static void handle_up(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto touch = event_receiver_struct->receiver;
#if HAVE_WLR_BASE_INPUT_DEVICES
    auto wlr_event = reinterpret_cast<wlr_touch_up_event*>(data);
#else
    auto wlr_event = reinterpret_cast<wlr_event_touch_up*>(data);
#endif

    auto event = touch_up_event{
        wlr_event->touch_id,
        {
            touch,
            wlr_event->time_msec,
        },
    };

    Q_EMIT touch->up(event);
}

static void handle_motion(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto touch = event_receiver_struct->receiver;
#if HAVE_WLR_BASE_INPUT_DEVICES
    auto wlr_event = reinterpret_cast<wlr_touch_motion_event*>(data);
#else
    auto wlr_event = reinterpret_cast<wlr_event_touch_motion*>(data);
#endif

    auto event = touch_motion_event{
        wlr_event->touch_id,
        QPointF(wlr_event->x, wlr_event->y),
        {
            touch,
            wlr_event->time_msec,
        },
    };

    Q_EMIT touch->motion(event);
}

static void handle_cancel(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto touch = event_receiver_struct->receiver;
#if HAVE_WLR_BASE_INPUT_DEVICES
    auto wlr_event = reinterpret_cast<wlr_touch_cancel_event*>(data);
#else
    auto wlr_event = reinterpret_cast<wlr_event_touch_cancel*>(data);
#endif

    auto event = touch_cancel_event{
        wlr_event->touch_id,
        {
            touch,
            wlr_event->time_msec,
        },
    };

    Q_EMIT touch->cancel(event);
}

static void handle_frame(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto touch = event_receiver_struct->receiver;

    Q_EMIT touch->frame();
}

touch::touch(wlr_input_device* dev, input::platform* platform)
    : input::touch(platform)
{
#if HAVE_WLR_BASE_INPUT_DEVICES
    auto backend = wlr_touch_from_input_device(dev);
#else
    auto backend = dev->touch;
#endif

    if (auto libinput = get_libinput_device(dev)) {
        control = std::make_unique<touch_control>(libinput, platform);
    }
    output = get_output();

    destroyed.receiver = this;
    destroyed.event.notify = handle_destroy;
    wl_signal_add(&dev->events.destroy, &destroyed.event);

    down_rec.receiver = this;
    down_rec.event.notify = handle_down;
    wl_signal_add(&backend->events.down, &down_rec.event);

    up_rec.receiver = this;
    up_rec.event.notify = handle_up;
    wl_signal_add(&backend->events.up, &up_rec.event);

    motion_rec.receiver = this;
    motion_rec.event.notify = handle_motion;
    wl_signal_add(&backend->events.motion, &motion_rec.event);

    cancel_rec.receiver = this;
    cancel_rec.event.notify = handle_cancel;
    wl_signal_add(&backend->events.cancel, &cancel_rec.event);

    frame_rec.receiver = this;
    frame_rec.event.notify = handle_frame;
    wl_signal_add(&backend->events.frame, &frame_rec.event);
}
}
