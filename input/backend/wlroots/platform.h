/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/platform.h"
#include "platform/wlroots.h"

namespace KWin::input::backend::wlroots
{

class platform : public input::platform
{
    Q_OBJECT
public:
    platform(platform_base::wlroots* base);
    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    platform(platform&& other) noexcept = default;
    platform& operator=(platform&& other) noexcept = default;
    ~platform();

private:
    event_receiver<platform> add_device;
    platform_base::wlroots* base;
};

}
