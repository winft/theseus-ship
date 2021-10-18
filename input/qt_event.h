/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "event.h"

#include <kwin_export.h>

namespace KWin::input
{

Qt::MouseButton KWIN_EXPORT button_to_qt_mouse_button(uint32_t button);
uint32_t qt_mouse_button_to_button(Qt::MouseButton button);

Qt::Key KWIN_EXPORT key_to_qt_key(uint32_t key);

QMouseEvent button_to_qt_event(button_event const& event);

QMouseEvent motion_to_qt_event(motion_event const& event);
QMouseEvent motion_absolute_to_qt_event(motion_absolute_event const& event);

QWheelEvent axis_to_qt_event(axis_event const& event);

QKeyEvent key_to_qt_event(key_event const& event);

}
