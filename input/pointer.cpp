/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "pointer.h"

#include "platform.h"
#include "utils/algorithm.h"

namespace KWin::input
{

pointer::pointer(input::platform* platform)
    : platform{platform}
{
}

}
