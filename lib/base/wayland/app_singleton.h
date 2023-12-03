/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <base/app_singleton.h>

namespace KWin::base::wayland
{

class app_singleton : public base::app_singleton
{
public:
    app_singleton(int& argc, char** argv)
    {
        qapp = std::make_unique<QApplication>(argc, argv);
        prepare_qapp();
    }
};

}
