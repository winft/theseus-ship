/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <algorithm>

namespace KWin::win::wayland
{

template<typename Window, typename Space>
void adopt_transient_children(Space* space, Window* window)
{
    auto const& wins = space->announced_windows;
    std::for_each(wins.cbegin(), wins.cend(), [&window](auto win) { win->checkTransient(window); });
}

}
