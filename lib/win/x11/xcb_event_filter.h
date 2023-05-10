/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "space_event.h"

#include <QAbstractNativeEventFilter>
#include <xcb/xcb.h>

namespace KWin::win::x11
{

template<typename Space>
class xcb_event_filter : public QAbstractNativeEventFilter
{
public:
    xcb_event_filter(Space& space)
        : space{space}
    {
    }

    bool
    nativeEventFilter(QByteArray const& event_type, void* message, qintptr* /*result*/) override
    {
        if (event_type != "xcb_generic_event_t") {
            return false;
        }

        auto event = static_cast<xcb_generic_event_t*>(message);
        update_time_from_event(space.base, event);

        return space_event(space, event);
    }

private:
    Space& space;
};

}
