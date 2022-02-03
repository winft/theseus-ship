/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"
#include "workspace.h"

#include <xcb/xcb.h>

namespace KWin::base::x11
{

class user_interaction_filter : public event_filter
{
public:
    user_interaction_filter()
        : event_filter(
            QVector<int>{XCB_KEY_PRESS, XCB_KEY_RELEASE, XCB_BUTTON_PRESS, XCB_BUTTON_RELEASE})
    {
    }

    bool event(xcb_generic_event_t* /*event*/) override
    {
        workspace()->setWasUserInteraction();
        return false;
    }
};

}
