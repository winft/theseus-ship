/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/platform.h"

namespace KWin::input::backend::x11
{

class xinput_integration;

class platform : public input::platform
{
    Q_OBJECT
public:
    platform();
    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    platform(platform&& other) noexcept = default;
    platform& operator=(platform&& other) noexcept = default;
    ~platform() override = default;

    std::unique_ptr<xinput_integration> xinput;
};

void create_cursor(platform* platform);

}
