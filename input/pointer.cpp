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
    platform->pointers.push_back(this);
}

pointer::~pointer()
{
    if (platform) {
        remove_all(platform->pointers, this);
        Q_EMIT platform->pointer_removed(this);
    }
}

}
