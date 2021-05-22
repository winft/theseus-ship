/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "touch.h"

#include "platform.h"
#include "utils.h"

namespace KWin::input::backend::wlroots
{

using er = event_receiver<touch>;

static void handle_destroy(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto touch = event_receiver_struct->receiver;

    touch->backend = nullptr;

    if (touch->plat) {
        remove_all(touch->plat->touchs, touch);
        Q_EMIT touch->plat->touch_removed(touch);
    }
}

static void handle_down(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto touch = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_event_touch_down*>(data);

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
    auto wlr_event = reinterpret_cast<wlr_event_touch_up*>(data);

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
    auto wlr_event = reinterpret_cast<wlr_event_touch_motion*>(data);

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
    auto wlr_event = reinterpret_cast<wlr_event_touch_cancel*>(data);

    auto event = touch_cancel_event{
        wlr_event->touch_id,
        {
            touch,
            wlr_event->time_msec,
        },
    };

    Q_EMIT touch->cancel(event);
}

touch::touch(wlr_input_device* dev, platform* plat)
    : input::touch(plat)
{
    backend = dev->touch;

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
}

}
