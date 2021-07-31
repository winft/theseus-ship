/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input.h"

namespace KWin::input
{

class terminate_server_filter : public InputEventFilter
{
public:
    bool keyEvent(QKeyEvent* event) override;
};

}
