/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

#include "cursor.h"

#include <QPoint>
#include <QRect>
#include <QTimer>

namespace KWin::win
{

struct move_resize_op {
    bool enabled{false};
    bool unrestricted{false};
    QPoint offset;
    QPoint inverted_offset;
    QRect initial_geometry;
    QRect geometry;
    win::position contact{win::position::center};
    bool button_down{false};
    CursorShape cursor{Qt::ArrowCursor};
    int start_screen{0};
    QTimer* delay_timer{nullptr};
};

}
