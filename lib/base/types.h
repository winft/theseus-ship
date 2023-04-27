/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

namespace KWin::base
{

enum class operation_mode {
    x11,
    wayland,
    xwayland,
};

inline bool should_use_wayland_for_compositing(operation_mode mode)
{
    return mode == operation_mode::wayland || mode == operation_mode::xwayland;
}

template<typename Base>
bool should_use_wayland_for_compositing(Base const& base)
{
    return base.operation_mode == operation_mode::wayland
        || base.operation_mode == operation_mode::xwayland;
}

}
