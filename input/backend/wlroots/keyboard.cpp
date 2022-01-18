/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard.h"

#include "control/headless/keyboard.h"
#include "control/keyboard.h"

#include "platform.h"

#include "main.h"
#include "utils.h"
#include "wayland_server.h"

extern "C" {
#include <wlr/backend/libinput.h>
}

namespace KWin::input::backend::wlroots
{

using er = base::event_receiver<keyboard>;

static void handle_destroy(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto keyboard = event_receiver_struct->receiver;

    keyboard->backend = nullptr;
    delete keyboard;
}

static void handle_key(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto keyboard = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_event_keyboard_key*>(data);

    auto event = key_event{
        wlr_event->keycode,
        static_cast<key_state>(wlr_event->state),
        wlr_event->update_state,
        {
            keyboard,
            wlr_event->time_msec,
        },
    };

    Q_EMIT keyboard->key_changed(event);
}

static void handle_modifiers(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto keyboard = event_receiver_struct->receiver;
    auto& mods = keyboard->backend->modifiers;

    auto event = modifiers_event{
        mods.depressed,
        mods.latched,
        mods.locked,
        mods.group,
        {
            keyboard,
        },
    };

    Q_EMIT keyboard->modifiers_changed(event);
}

keyboard::keyboard(wlr_input_device* dev, input::platform* platform)
    : input::keyboard(platform)
{
    backend = dev->keyboard;

    if (auto libinput = get_libinput_device(dev)) {
        control = std::make_unique<keyboard_control>(libinput, platform);
    } else if (is_headless_device(dev)) {
        auto headless_control = std::make_unique<headless::keyboard_control>(platform);
        headless_control->data.is_alpha_numeric_keyboard = true;
        this->control = std::move(headless_control);
    }

    destroyed.receiver = this;
    destroyed.event.notify = handle_destroy;
    wl_signal_add(&backend->events.destroy, &destroyed.event);

    key_rec.receiver = this;
    key_rec.event.notify = handle_key;
    wl_signal_add(&backend->events.key, &key_rec.event);

    modifiers_rec.receiver = this;
    modifiers_rec.event.notify = handle_modifiers;
    wl_signal_add(&backend->events.modifiers, &modifiers_rec.event);
}

}
