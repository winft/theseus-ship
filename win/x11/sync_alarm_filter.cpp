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
#include "workspace.h"

#include "base/x11/xcb/extensions.h"
#include "win/x11/geo.h"
#include "win/x11/window.h"

#include <xcb/sync.h>

namespace KWin::win::x11
{

sync_alarm_filter::sync_alarm_filter()
    : base::x11::event_filter(
        QVector<int>{base::x11::xcb::extensions::self()->sync_alarm_notify_event()})
{
}

bool sync_alarm_filter::event(xcb_generic_event_t* event)
{
    auto alarmEvent = reinterpret_cast<xcb_sync_alarm_notify_event_t*>(event);
    auto client = workspace()->findAbstractClient([alarmEvent](Toplevel const* client) {
        auto x11_client = qobject_cast<win::x11::window const*>(client);
        if (!x11_client) {
            return false;
        }
        auto const sync_request = x11_client->sync_request;
        return alarmEvent->alarm == sync_request.alarm;
    });
    if (client) {
        win::x11::handle_sync(static_cast<win::x11::window*>(client), alarmEvent->counter_value);
    }
    return false;
}

} // namespace KWin
