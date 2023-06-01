/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "geo.h"

#include "base/x11/event_filter.h"
#include "base/x11/xcb/extensions.h"

#include <xcb/sync.h>

namespace KWin::win::x11
{

template<typename Space>
class sync_alarm_filter : public base::x11::event_filter
{
public:
    sync_alarm_filter(Space& space)
        : base::x11::event_filter(
            *space.base.x11_event_filters,
            QVector<int>{base::x11::xcb::extensions::self()->sync_alarm_notify_event()})
        , space{space}
    {
    }

    bool event(xcb_generic_event_t* event) override
    {
        auto alarmEvent = reinterpret_cast<xcb_sync_alarm_notify_event_t*>(event);

        for (auto win : space.windows) {
            if (std::visit(overload{[&](typename Space::x11_window* win) {
                                        if (!win->control) {
                                            return false;
                                        }
                                        if (alarmEvent->alarm != win->sync_request.alarm) {
                                            return false;
                                        }
                                        handle_sync(win, alarmEvent->counter_value);
                                        return true;
                                    },
                                    [](auto&&) { return false; }},
                           win)) {
                break;
            }
        }

        return false;
    }

private:
    Space& space;
};

}
