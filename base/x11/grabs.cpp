/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "grabs.h"

#include "base/logging.h"
#include "utils/memory.h"

#include <QApplication>
#include <QWidget>
#include <cassert>

namespace KWin::base::x11
{

static int server_grab_count = 0;

void grab_server(xcb_connection_t* con)
{
    if (++server_grab_count == 1) {
        xcb_grab_server(con);
    }
}

void ungrab_server(xcb_connection_t* con)
{
    assert(server_grab_count > 0);
    if (--server_grab_count == 0) {
        xcb_ungrab_server(con);
        xcb_flush(con);
    }
}

static bool keyboard_grabbed = false;

bool grab_keyboard(x11::data const& data, xcb_window_t w)
{
    if (QWidget::keyboardGrabber() != nullptr) {
        return false;
    }
    if (keyboard_grabbed) {
        qCDebug(KWIN_CORE) << "Failed to grab X Keyboard: already grabbed by us";
        return false;
    }
    if (qApp->activePopupWidget() != nullptr) {
        qCDebug(KWIN_CORE) << "Failed to grab X Keyboard: no popup widget";
        return false;
    }

    if (w == XCB_WINDOW_NONE) {
        w = data.root_window;
    }

    auto const cookie = xcb_grab_keyboard_unchecked(
        data.connection, false, w, data.time, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
    unique_cptr<xcb_grab_keyboard_reply_t> grab(
        xcb_grab_keyboard_reply(data.connection, cookie, nullptr));

    if (!grab) {
        qCDebug(KWIN_CORE) << "Failed to grab X Keyboard: grab null";
        return false;
    }
    if (grab->status != XCB_GRAB_STATUS_SUCCESS) {
        qCDebug(KWIN_CORE) << "Failed to grab X Keyboard: grab failed with status" << grab->status;
        return false;
    }

    keyboard_grabbed = true;
    return true;
}

void ungrab_keyboard(xcb_connection_t* con)
{
    if (!keyboard_grabbed) {
        // grabXKeyboard() may fail sometimes, so don't fail, but at least warn anyway
        qCDebug(KWIN_CORE) << "ungrabXKeyboard() called but keyboard not grabbed!";
    }

    keyboard_grabbed = false;
    xcb_ungrab_keyboard(con, XCB_TIME_CURRENT_TIME);
}

}
