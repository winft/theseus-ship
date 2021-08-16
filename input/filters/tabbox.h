/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "../event_filter.h"

namespace KWin::input
{

class tabbox_filter : public event_filter
{
public:
    bool keyEvent(QKeyEvent* event) override;

    bool button(button_event const& event) override;
    bool motion(motion_event const& event) override;

    bool wheelEvent(QWheelEvent* event) override;
};

}
