/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "space.h"

namespace KWin::win
{

space_qobject::space_qobject(std::function<void()> reconfigure_callback)
    : reconfigure_callback{reconfigure_callback}
{
}

void space_qobject::reconfigure()
{
    reconfigure_callback();
}

}
