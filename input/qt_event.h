/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

namespace KWin::input
{

Qt::MouseButton button_to_qt_mouse_button(uint32_t button);
uint32_t qt_mouse_button_to_button(Qt::MouseButton button);

}
