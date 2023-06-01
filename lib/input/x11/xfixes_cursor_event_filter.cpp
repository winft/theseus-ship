/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "xfixes_cursor_event_filter.h"

#include "base/x11/xcb/extensions.h"
#include "cursor.h"

namespace KWin::input::x11
{

xfixes_cursor_event_filter::xfixes_cursor_event_filter(base::x11::event_filter_manager& manager,
                                                       x11::cursor* cursor)
    : base::x11::event_filter(
        manager,
        QVector<int>{base::x11::xcb::extensions::self()->fixes_cursor_notify_event()})
    , m_cursor(cursor)
{
}

bool xfixes_cursor_event_filter::event(xcb_generic_event_t* event)
{
    Q_UNUSED(event);
    m_cursor->notify_cursor_changed();
    return false;
}

}
