/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <xcb/xcb.h>

namespace KWin::base::x11
{

void KWIN_EXPORT grab_server();
void KWIN_EXPORT ungrab_server();

bool KWIN_EXPORT grab_keyboard(xcb_window_t w = XCB_WINDOW_NONE);
void KWIN_EXPORT ungrab_keyboard();

/**
 * Small helper class which performs grabXServer in the ctor and
 * ungrabXServer in the dtor. Use this class to ensure that grab and
 * ungrab are matched.
 */
class server_grabber
{
public:
    server_grabber()
    {
        grab_server();
    }
    ~server_grabber()
    {
        ungrab_server();
    }
};
}
