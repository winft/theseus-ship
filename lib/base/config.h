/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <KSharedConfig>

namespace KWin::base
{

class config
{
public:
    config(KConfig::OpenFlag open_mode)
        : main{KSharedConfig::openConfig({}, open_mode)}
    {
    }

    KSharedConfigPtr main;
};

}
