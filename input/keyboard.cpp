/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard.h"

#include "utils/algorithm.h"

namespace KWin::input
{

keyboard::keyboard(xkb_context* context, xkb_compose_table* compose_table)
    : xkb{std::make_unique<xkb::keyboard>(context, compose_table)}
{
}

}
