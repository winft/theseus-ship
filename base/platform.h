/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <vector>

namespace KWin::base
{

template<typename Backend, typename Output>
class platform
{
public:
    platform() = default;

    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    platform(platform&& other) noexcept = default;
    platform& operator=(platform&& other) noexcept = default;
    ~platform() = default;

    Backend backend;
    std::vector<Output*> all_outputs;
    std::vector<Output*> enabled_outputs;
};

}
