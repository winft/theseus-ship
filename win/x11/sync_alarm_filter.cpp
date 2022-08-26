/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2017 Martin Flöser <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "sync_alarm_filter.h"

#include "base/x11/xcb/extensions.h"
#include "win/space.h"
#include "win/x11/geo.h"
#include "win/x11/window.h"

#include <xcb/sync.h>

namespace KWin::win::x11
{

sync_alarm_filter::sync_alarm_filter(win::space& space)
    : base::x11::event_filter(
        QVector<int>{base::x11::xcb::extensions::self()->sync_alarm_notify_event()})
    , space{space}
{
}

bool sync_alarm_filter::event(xcb_generic_event_t* event)
{
    auto alarmEvent = reinterpret_cast<xcb_sync_alarm_notify_event_t*>(event);

    for (auto win : space.windows) {
        if (!win->control) {
            continue;
        }
        auto x11_win = dynamic_cast<win::x11::window*>(win);
        if (!x11_win) {
            continue;
        }
        if (alarmEvent->alarm == x11_win->sync_request.alarm) {
            handle_sync(x11_win, alarmEvent->counter_value);
            break;
        }
    }

    return false;
}

} // namespace KWin
