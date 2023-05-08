/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "property_window.h"

namespace KWin::win
{

class window_qobject;

class script_window : public property_window
{
public:
    script_window(window_qobject& qtwin)
        : property_window(qtwin)
    {
    }
};

}
