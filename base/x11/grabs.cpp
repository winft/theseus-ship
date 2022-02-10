/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "grabs.h"

#include "base/logging.h"
#include "main.h"
#include "utils/memory.h"

#include <QWidget>
#include <cassert>

namespace KWin::base::x11
{

static int server_grab_count = 0;

void grab_server()
{
    if (++server_grab_count == 1) {
        xcb_grab_server(connection());
    }
}

void ungrab_server()
{
    assert(server_grab_count > 0);
    if (--server_grab_count == 0) {
        xcb_ungrab_server(connection());
        xcb_flush(connection());
    }
}

static bool keyboard_grabbed = false;

bool grab_keyboard(xcb_window_t w)
{
    if (QWidget::keyboardGrabber() != nullptr) {
        return false;
    }
    if (keyboard_grabbed) {
        return false;
    }
    if (qApp->activePopupWidget() != nullptr) {
        return false;
    }

    if (w == XCB_WINDOW_NONE) {
        w = rootWindow();
    }

    auto const cookie = xcb_grab_keyboard_unchecked(
        connection(), false, w, xTime(), XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
    unique_cptr<xcb_grab_keyboard_reply_t> grab(
        xcb_grab_keyboard_reply(connection(), cookie, nullptr));

    if (!grab) {
        return false;
    }
    if (grab->status != XCB_GRAB_STATUS_SUCCESS) {
        return false;
    }

    keyboard_grabbed = true;
    return true;
}

void ungrab_keyboard()
{
    if (!keyboard_grabbed) {
        // grabXKeyboard() may fail sometimes, so don't fail, but at least warn anyway
        qCDebug(KWIN_CORE) << "ungrabXKeyboard() called but keyboard not grabbed!";
    }

    keyboard_grabbed = false;
    xcb_ungrab_keyboard(connection(), XCB_TIME_CURRENT_TIME);
}

}
