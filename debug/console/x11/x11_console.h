/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "../console.h"

namespace KWin
{
namespace debug
{

class KWIN_EXPORT x11_console : public console
{
    Q_OBJECT
public:
    x11_console();
};

}
}
