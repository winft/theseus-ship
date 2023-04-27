/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window.h"

namespace KWin::scripting
{

window::window(win::window_qobject& qtwin)
    : property_window(qtwin)
{
}

QStringList window::activities() const
{
    return {};
}

bool window::isShadeable() const
{
    return false;
}

bool window::isShade() const
{
    return false;
}

void window::setShade(bool /*set*/)
{
}

}
