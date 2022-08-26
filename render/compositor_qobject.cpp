/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "compositor_qobject.h"

namespace KWin::render
{

compositor_qobject::compositor_qobject(std::function<bool(QTimerEvent*)> timer_event_handler)
    : timer_event_handler{timer_event_handler}
{
}

void compositor_qobject::timerEvent(QTimerEvent* te)
{
    if (!timer_event_handler(te)) {
        QObject::timerEvent(te);
    }
}

}
