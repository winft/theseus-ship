/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "singleton_interface.h"

namespace KWin::input
{

input::cursor* singleton_interface::cursor{nullptr};
input::platform_qobject* singleton_interface::platform_qobject{nullptr};

}
