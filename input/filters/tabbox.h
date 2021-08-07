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
    bool pointerEvent(QMouseEvent* event, quint32 button) override;
    bool wheelEvent(QWheelEvent* event) override;
};

}
