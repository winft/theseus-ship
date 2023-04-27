/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "compositor_qobject.h"

#include "singleton_interface.h"

namespace KWin::render
{

compositor_qobject::compositor_qobject(std::function<bool(QTimerEvent*)> timer_event_handler)
    : timer_event_handler{timer_event_handler}
{
    singleton_interface::compositor = this;
}

compositor_qobject::~compositor_qobject()
{
    singleton_interface::compositor = nullptr;
}

void compositor_qobject::timerEvent(QTimerEvent* te)
{
    if (!timer_event_handler(te)) {
        QObject::timerEvent(te);
    }
}

}
