/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"

#include "base/os/kkeyserver.h"
#include "win/x11/event.h"
#include "win/x11/stacking.h"

#include <xcb/xcb.h>

namespace KWin::win::x11
{

template<typename Space>
class moving_window_filter : public base::x11::event_filter
{
public:
    moving_window_filter(Space& space)
        : base::x11::event_filter(
            QVector<int>{XCB_KEY_PRESS, XCB_MOTION_NOTIFY, XCB_BUTTON_PRESS, XCB_BUTTON_RELEASE})
        , space{space}
    {
    }

    bool event(xcb_generic_event_t* event) override
    {
        auto mr_win = dynamic_cast<typename Space::x11_window*>(space.move_resize_window);
        if (!mr_win) {
            return false;
        }

        auto handle_event = [mr_win, event](auto event_win) {
            return mr_win->xcb_windows.grab == event_win && window_event(mr_win, event);
        };

        uint8_t const eventType = event->response_type & ~0x80;

        switch (eventType) {
        case XCB_KEY_PRESS: {
            int keyQt;
            xcb_key_press_event_t* keyEvent = reinterpret_cast<xcb_key_press_event_t*>(event);
            KKeyServer::xcbKeyPressEventToQt(keyEvent, &keyQt);
            key_press_event(mr_win, keyQt, keyEvent->time);
            return true;
        }
        case XCB_BUTTON_PRESS:
        case XCB_BUTTON_RELEASE:
            return handle_event(reinterpret_cast<xcb_button_press_event_t*>(event)->event);
        case XCB_MOTION_NOTIFY:
            return handle_event(reinterpret_cast<xcb_motion_notify_event_t*>(event)->event);
        }

        return false;
    }

private:
    Space& space;
};

}
