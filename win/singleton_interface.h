/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <functional>
#include <string>

namespace KWin::win
{

class space;

/// Only for exceptional use in environments without dependency injection support (e.g. Qt plugins).
struct KWIN_EXPORT singleton_interface {
    static win::space* space;
    static std::function<std::string(std::string const&)> set_activation_token;
};

}
