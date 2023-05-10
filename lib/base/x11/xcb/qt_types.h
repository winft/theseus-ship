/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <Qt>
#include <xcb/xcb.h>

namespace KWin::base::x11::xcb
{

inline Qt::MouseButton to_qt_mouse_button(int button)
{
    if (button == XCB_BUTTON_INDEX_1) {
        return Qt::LeftButton;
    }
    if (button == XCB_BUTTON_INDEX_2) {
        return Qt::MiddleButton;
    }
    if (button == XCB_BUTTON_INDEX_3) {
        return Qt::RightButton;
    }
    if (button == XCB_BUTTON_INDEX_4) {
        return Qt::XButton1;
    }
    if (button == XCB_BUTTON_INDEX_5) {
        return Qt::XButton2;
    }
    return Qt::NoButton;
}

inline Qt::MouseButtons to_qt_mouse_buttons(int state)
{
    Qt::MouseButtons ret = {};

    if (state & XCB_KEY_BUT_MASK_BUTTON_1) {
        ret |= Qt::LeftButton;
    }
    if (state & XCB_KEY_BUT_MASK_BUTTON_2) {
        ret |= Qt::MiddleButton;
    }
    if (state & XCB_KEY_BUT_MASK_BUTTON_3) {
        ret |= Qt::RightButton;
    }
    if (state & XCB_KEY_BUT_MASK_BUTTON_4) {
        ret |= Qt::XButton1;
    }
    if (state & XCB_KEY_BUT_MASK_BUTTON_5) {
        ret |= Qt::XButton2;
    }

    return ret;
}

}
