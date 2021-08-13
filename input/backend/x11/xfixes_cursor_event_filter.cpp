/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "xfixes_cursor_event_filter.h"
#include "cursor.h"
#include "xcbutils.h"

namespace KWin::input::backend::x11
{

xfixes_cursor_event_filter::xfixes_cursor_event_filter(cursor* cursor)
    : KWin::platform::x11::event_filter(
        QVector<int>{Xcb::Extensions::self()->fixesCursorNotifyEvent()})
    , m_cursor(cursor)
{
}

bool xfixes_cursor_event_filter::event(xcb_generic_event_t* event)
{
    Q_UNUSED(event);
    m_cursor->notifyCursorChanged();
    return false;
}

}
