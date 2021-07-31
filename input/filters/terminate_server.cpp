/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "terminate_server.h"

#include "utils.h"
#include "xkb.h"

#include <QKeyEvent>

namespace KWin::input
{

bool terminate_server_filter::keyEvent(QKeyEvent* event)
{
    if (event->type() == QEvent::KeyPress && !event->isAutoRepeat()) {
        if (event->nativeVirtualKey() == XKB_KEY_Terminate_Server) {
            qCWarning(KWIN_CORE) << "Request to terminate server";
            QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
            return true;
        }
    }
    return false;
}

}
