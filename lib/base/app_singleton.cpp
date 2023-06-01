/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "app_singleton.h"

#include "singleton_interface.h"

namespace KWin::base
{

app_singleton::app_singleton()
{
    singleton_interface::app_singleton = this;
}

}
