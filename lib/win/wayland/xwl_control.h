/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control_destroy.h"
#include "desktop_set.h"

#include "win/x11/control.h"

namespace KWin::win::wayland
{

template<typename Win>
class xwl_control : public x11::control<Win>
{
public:
    xwl_control(Win* window)
        : x11::control<Win>(window)
        , window{window}
    {
    }

    void set_desktops(QVector<virtual_desktop*> desktops) override
    {
        wayland::desktops_announce(*window, desktops);
        x11::control<Win>::set_desktops(desktops);
    }

    void destroy_plasma_wayland_integration() override
    {
        destroy_plasma_integration(*this);
    }

private:
    Win* window;
};

}
