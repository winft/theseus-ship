/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

namespace KWin::base::wayland
{

template<typename Base>
bool is_screen_locked(Base const& base)
{
    if constexpr (requires(decltype(base) base) { base.server; }) {
        return base.server->is_screen_locked();
    }
    return false;
}

}
