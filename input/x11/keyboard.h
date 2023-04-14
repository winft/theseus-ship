/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "xkb.h"

#include "base/x11/xcb/extensions.h"
#include "input/keyboard.h"

#include <xcb/xcb.h>

namespace KWin::input::x11
{

class keyboard : public input::keyboard
{
public:
    template<typename Base>
    keyboard(Base& base, xkb_context* context, xkb_compose_table* compose_table)
        : input::keyboard(context, compose_table)
        , connection{base.x11_data.connection}
    {
        xkb_device_id = xkb_get_device_id(connection);
        xkb_select_events(connection, xkb_device_id);
        xkb_update_keymap(*this);

        auto xkb_event_type = base::x11::xcb::extensions::self()->xkb_event_base();
        xkb_filter = std::make_unique<x11::xkb_filter<x11::keyboard>>(
            xkb_event_type, *this, *base.x11_event_filters);
    }

    xcb_connection_t* connection;
    int xkb_device_id{-1};
    std::unique_ptr<x11::xkb_filter<x11::keyboard>> xkb_filter;
};

}
