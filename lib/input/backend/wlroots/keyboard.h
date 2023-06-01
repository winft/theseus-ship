/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control/headless/keyboard.h"
#include "control/keyboard.h"
#include "platform.h"

#include "base/utils.h"
#include "config-kwin.h"
#include "input/keyboard.h"

extern "C" {
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
}

namespace KWin::input::backend::wlroots
{

template<typename Platform>
class keyboard;

template<typename Platform>
void keyboard_handle_destroy(struct wl_listener* listener, void* /*data*/)
{
    base::event_receiver<keyboard<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto keyboard = event_receiver_struct->receiver;

    keyboard->backend = nullptr;
    platform_remove_keyboard(keyboard, *keyboard->platform);
    delete keyboard;
}

template<typename Platform>
void handle_key(struct wl_listener* listener, void* data)
{
    base::event_receiver<keyboard<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
    auto keyboard = event_receiver_struct->receiver;
    auto wlr_event = reinterpret_cast<wlr_keyboard_key_event*>(data);

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

template<typename Platform>
void handle_modifiers(struct wl_listener* listener, void* /*data*/)
{
    base::event_receiver<keyboard<Platform>>* event_receiver_struct
        = wl_container_of(listener, event_receiver_struct, event);
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

template<typename Platform>
class keyboard : public input::keyboard
{
public:
    keyboard(wlr_input_device* dev, Platform* platform)
        : input::keyboard(platform->xkb.context, platform->xkb.compose_table)
        , platform{platform}
    {
        backend = wlr_keyboard_from_input_device(dev);

        if (auto libinput = get_libinput_device(dev)) {
            control = std::make_unique<keyboard_control>(libinput, platform->config.main);
        } else if (base::backend::wlroots::get_headless_backend(platform->base.backend)) {
            auto headless_control = std::make_unique<headless::keyboard_control>();
            headless_control->data.is_alpha_numeric_keyboard = true;
            this->control = std::move(headless_control);
        }

        destroyed.receiver = this;
        destroyed.event.notify = keyboard_handle_destroy<Platform>;

        wl_signal_add(&backend->base.events.destroy, &destroyed.event);

        key_rec.receiver = this;
        key_rec.event.notify = handle_key<Platform>;
        wl_signal_add(&backend->events.key, &key_rec.event);

        modifiers_rec.receiver = this;
        modifiers_rec.event.notify = handle_modifiers<Platform>;
        wl_signal_add(&backend->events.modifiers, &modifiers_rec.event);
    }

    keyboard(keyboard const&) = delete;
    keyboard& operator=(keyboard const&) = delete;
    ~keyboard() override = default;

    Platform* platform;
    wlr_keyboard* backend{nullptr};

private:
    using er = base::event_receiver<keyboard<Platform>>;
    er destroyed;
    er key_rec;
    er modifiers_rec;
};

}
