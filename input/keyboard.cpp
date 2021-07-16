/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard.h"

namespace KWin::input
{

keyboard::keyboard(platform* plat, QObject* parent)
    : QObject(parent)
    , plat{plat}
{
}

keyboard::~keyboard()
{
}

}
