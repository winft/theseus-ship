/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard.h"

#include "control/headless/keyboard.h"
#include "control/keyboard.h"
#include "platform.h"

#include "base/wayland/server.h"
#include "main.h"

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
    platform_remove_keyboard(keyboard, *keyboard->platform);
    delete keyboard;
}

static void handle_key(struct wl_listener* listener, [[maybe_unused]] void* data)
{
    er* event_receiver_struct = wl_container_of(listener, event_receiver_struct, event);
    auto keyboard = event_receiver_struct->receiver;
#if HAVE_WLR_BASE_INPUT_DEVICES
    auto wlr_event = reinterpret_cast<wlr_keyboard_key_event*>(data);
#else
    auto wlr_event = reinterpret_cast<wlr_event_keyboard_key*>(data);
#endif

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
#if HAVE_WLR_BASE_INPUT_DEVICES
    backend = wlr_keyboard_from_input_device(dev);
#else
    backend = dev->keyboard;
#endif

    if (auto libinput = get_libinput_device(dev)) {
        control = std::make_unique<keyboard_control>(libinput, platform->config);
    } else if (base::backend::wlroots::get_headless_backend(
                   static_cast<wlroots::platform*>(platform)->base.backend)) {
        auto headless_control = std::make_unique<headless::keyboard_control>();
        headless_control->data.is_alpha_numeric_keyboard = true;
        this->control = std::move(headless_control);
    }

    destroyed.receiver = this;
    destroyed.event.notify = handle_destroy;

#if HAVE_WLR_BASE_INPUT_DEVICES
    wl_signal_add(&backend->base.events.destroy, &destroyed.event);
#else
    wl_signal_add(&backend->events.destroy, &destroyed.event);
#endif

    key_rec.receiver = this;
    key_rec.event.notify = handle_key;
    wl_signal_add(&backend->events.key, &key_rec.event);

    modifiers_rec.receiver = this;
    modifiers_rec.event.notify = handle_modifiers;
    wl_signal_add(&backend->events.modifiers, &modifiers_rec.event);
}

}
