/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "keyboard.h"
#include "pointer.h"
#include "touch.h"

#include "input/platform.h"

namespace KWin::input::wayland::fake
{

template<typename Platform>
class devices
{
public:
    devices(devices&&) noexcept = default;
    devices& operator=(devices&&) noexcept = default;

    explicit devices(Platform& platform, Wrapland::Server::FakeInputDevice* device)
        : platform{platform}
        , pointer{std::make_unique<fake::pointer<Platform>>(device, &platform)}
        , keyboard{std::make_unique<fake::keyboard<Platform>>(device, &platform)}
        , touch{std::make_unique<fake::touch<Platform>>(device, &platform)}
    {
        platform_add_pointer(pointer.get(), platform);
        platform_add_keyboard(keyboard.get(), platform);
        platform_add_touch(touch.get(), platform);
    }

    ~devices()
    {
        if (pointer) {
            platform_remove_pointer(pointer.get(), platform);
        }
        if (keyboard) {
            platform_remove_keyboard(keyboard.get(), platform);
        }
        if (touch) {
            platform_remove_touch(touch.get(), platform);
        }
    }

    std::unique_ptr<fake::pointer<Platform>> pointer;
    std::unique_ptr<fake::keyboard<Platform>> keyboard;
    std::unique_ptr<fake::touch<Platform>> touch;
    Platform& platform;
};

}
