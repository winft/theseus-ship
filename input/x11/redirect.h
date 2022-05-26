/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/redirect.h"

namespace KWin::input::x11
{

class KWIN_EXPORT redirect : public input::redirect
{
    Q_OBJECT
public:
    redirect(input::platform& platform, win::space& space);
};

}
