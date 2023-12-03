/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "app_singleton.h"

#include "singleton_interface.h"

namespace KWin::base
{

app_singleton::app_singleton(int& argc, char** argv)
{
    singleton_interface::app_singleton = this;
    qapp = std::make_unique<QApplication>(argc, argv);

    qapp->setQuitOnLastWindowClosed(false);
    qapp->setQuitLockEnabled(false);
}

}
