/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <Wrapland/Server/plasma_window.h>

namespace KWin::win::wayland
{

template<typename Control>
void destroy_plasma_integration(Control& control)
{
    auto& pwi = control.plasma_wayland_integration;
    if (!pwi) {
        return;
    }
    pwi->unmap();
    pwi = nullptr;
}

}
