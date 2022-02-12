/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "terminate_server.h"

#include "input/event.h"
#include "input/keyboard.h"
#include "input/keyboard_redirect.h"
#include "input/logging.h"
#include "input/xkb/keyboard.h"
#include "main.h"

namespace KWin::input
{

bool terminate_server_filter::key(key_event const& event)
{
    if (event.state == key_state::pressed) {
        if (event.base.dev->xkb->to_keysym(event.keycode) == XKB_KEY_Terminate_Server) {
            qCWarning(KWIN_INPUT) << "Request to terminate server";
            QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
            return true;
        }
    }
    return false;
}

}
