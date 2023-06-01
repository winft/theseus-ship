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

template<typename Redirect>
class devices
{
public:
    devices(devices&&) noexcept = default;
    devices& operator=(devices&&) noexcept = default;

    explicit devices(Redirect& redirect, Wrapland::Server::FakeInputDevice* device)
        : redirect{redirect}
        , pointer{std::make_unique<fake::pointer<Redirect>>(device, redirect)}
        , keyboard{std::make_unique<fake::keyboard<Redirect>>(device, redirect)}
        , touch{std::make_unique<fake::touch<Redirect>>(device, redirect)}
    {
        platform_add_pointer(pointer.get(), redirect.platform);
        platform_add_keyboard(keyboard.get(), redirect.platform);
        platform_add_touch(touch.get(), redirect.platform);
    }

    ~devices()
    {
        if (pointer) {
            platform_remove_pointer(pointer.get(), redirect.platform);
        }
        if (keyboard) {
            platform_remove_keyboard(keyboard.get(), redirect.platform);
        }
        if (touch) {
            platform_remove_touch(touch.get(), redirect.platform);
        }
    }

    Redirect& redirect;
    std::unique_ptr<fake::pointer<Redirect>> pointer;
    std::unique_ptr<fake::keyboard<Redirect>> keyboard;
    std::unique_ptr<fake::touch<Redirect>> touch;
};

}
