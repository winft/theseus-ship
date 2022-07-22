/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "desktop_set.h"

#include "win/control.h"

namespace KWin::win::wayland
{

template<typename Win>
class control : public win::control
{
public:
    control(Win& window)
        : win::control(&window)
        , window{window}
    {
    }

    void set_desktops(QVector<virtual_desktop*> desktops) override
    {
        wayland::set_desktops(window, desktops);
    }

private:
    Win& window;
};

}
