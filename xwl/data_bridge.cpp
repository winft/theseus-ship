/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2018 Roman Gilg <subdiff@gmail.com>

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
#include "data_bridge.h"

#include "clipboard.h"
#include "dnd.h"
#include "primary_selection.h"

namespace KWin::xwl
{

data_bridge::data_bridge(x11_data const& x11)
    : x11{x11}
{
    xcb_prefetch_extension_data(x11.connection, &xcb_xfixes_id);
    xfixes = xcb_get_extension_data(x11.connection, &xcb_xfixes_id);

    clipboard.reset(new xwl::clipboard(x11));
    dnd.reset(new drag_and_drop(x11));
    primary_selection.reset(new xwl::primary_selection(x11));
}

data_bridge::~data_bridge() = default;

bool data_bridge::filter_event(xcb_generic_event_t* event)
{
    if (xwl::filter_event(clipboard.get(), event)) {
        return true;
    }
    if (xwl::filter_event(dnd.get(), event)) {
        return true;
    }
    if (xwl::filter_event(primary_selection.get(), event)) {
        return true;
    }
    if (event->response_type - xfixes->first_event == XCB_XFIXES_SELECTION_NOTIFY) {
        return handle_xfixes_notify(reinterpret_cast<xcb_xfixes_selection_notify_event_t*>(event));
    }
    return false;
}

bool data_bridge::handle_xfixes_notify(xcb_xfixes_selection_notify_event_t* event)
{
    if (event->selection == x11.atoms->clipboard) {
        return xwl::handle_xfixes_notify(clipboard.get(), event);
    }
    if (event->selection == x11.atoms->primary_selection) {
        return xwl::handle_xfixes_notify(primary_selection.get(), event);
    }
    if (event->selection == x11.atoms->xdnd_selection) {
        return xwl::handle_xfixes_notify(dnd.get(), event);
    }
    return false;
}

drag_event_reply data_bridge::drag_move_filter(Toplevel* target, QPoint const& pos)
{
    if (!dnd) {
        return drag_event_reply::wayland;
    }
    return dnd->drag_move_filter(target, pos);
}

}
