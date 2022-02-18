/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "event.h"
#include "kwin_export.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

namespace KWin::input
{

namespace xkb
{
class keyboard;
}

Qt::MouseButton KWIN_EXPORT button_to_qt_mouse_button(uint32_t button);
uint32_t KWIN_EXPORT qt_mouse_button_to_button(Qt::MouseButton button);

Qt::Key KWIN_EXPORT key_to_qt_key(uint32_t key, xkb::keyboard* xkb);

QMouseEvent KWIN_EXPORT button_to_qt_event(button_event const& event);

QMouseEvent KWIN_EXPORT motion_to_qt_event(motion_event const& event);
QMouseEvent motion_absolute_to_qt_event(motion_absolute_event const& event);

QWheelEvent KWIN_EXPORT axis_to_qt_event(axis_event const& event);

QKeyEvent KWIN_EXPORT key_to_qt_event(key_event const& event);

}
