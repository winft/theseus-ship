/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <KSharedConfig>
#include <string>

namespace KWin::base
{

class config
{
public:
    config(KConfig::OpenFlag open_mode, std::string const& file_name)
        : main{KSharedConfig::openConfig(QString::fromStdString(file_name), open_mode)}
    {
    }

    KSharedConfigPtr main;
};

}
