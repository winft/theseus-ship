/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/types.h"
#include "utils/flags.h"

namespace KWin::render::x11
{

enum class suspend_reason {
    none = 0,
    user = 1 << 0,
    rule = 1 << 1,
    script = 1 << 2,
    all = 0xff,
};

}

ENUM_FLAGS(KWin::render::x11::suspend_reason)
