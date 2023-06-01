/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control.h"

namespace KWin::win::wayland
{

template<typename Win>
class xdg_shell_control : public wayland::control<Win>
{
public:
    xdg_shell_control(Win& win)
        : wayland::control<Win>(win)
        , m_window{win}
    {
    }

    bool can_fullscreen() const override
    {
        if (!this->rules.checkFullScreen(true)) {
            return false;
        }
        return !is_special_window(&m_window);
    }

private:
    Win& m_window;
};

}
