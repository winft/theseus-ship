/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <functional>
#include <vector>

namespace KWin::base
{

class app_singleton;
class output;
class platform;

/// Only for exceptional use in environments without dependency injection support (e.g. Qt plugins).
struct KWIN_EXPORT singleton_interface {
    static base::app_singleton* app_singleton;
    static base::platform* platform;
    static std::function<std::vector<output*>()> get_outputs;
};

}
