/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"

#include <cassert>
#include <functional>
#include <xcb/xcb.h>

namespace KWin::base::x11
{

class user_interaction_filter : public event_filter
{
public:
    user_interaction_filter(event_filter_manager& manager, std::function<void(void)> callback)
        : event_filter(
            manager,
            QVector<int>{XCB_KEY_PRESS, XCB_KEY_RELEASE, XCB_BUTTON_PRESS, XCB_BUTTON_RELEASE})
        , callback{callback}
    {
        assert(callback);
    }

    bool event(xcb_generic_event_t* /*event*/) override
    {
        callback();
        return false;
    }

private:
    std::function<void(void)> callback;
};

}
