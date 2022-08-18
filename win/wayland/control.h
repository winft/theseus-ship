/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control_destroy.h"
#include "desktop_set.h"

#include "win/control.h"

namespace KWin::win::wayland
{

template<typename Win>
class control : public win::control<typename Win::abstract_type>
{
public:
    control(Win& window)
        : win::control<typename Win::abstract_type>(&window)
        , window{window}
    {
    }

    void set_desktops(QVector<virtual_desktop*> desktops) override
    {
        wayland::set_desktops(window, desktops);
    }

    void destroy_plasma_wayland_integration() override
    {
        destroy_plasma_integration(*this);
    }

private:
    Win& window;
};

}
