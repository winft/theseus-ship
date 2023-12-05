/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "singleton_interface.h"

namespace KWin::base
{

base::app_singleton* singleton_interface::app_singleton{nullptr};
base::platform_qobject* singleton_interface::platform{nullptr};
std::function<std::vector<output*>()> singleton_interface::get_outputs{};

}
