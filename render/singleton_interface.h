/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

namespace KWin::render
{

class platform;

/// Only for exceptional use in environments without dependency injection support (e.g. Qt plugins).
struct KWIN_EXPORT singleton_interface {
    static render::platform* platform;
};

}
