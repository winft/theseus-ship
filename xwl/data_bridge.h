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
#pragma once

#include "clipboard.h"
#include "dnd.h"
#include "primary_selection.h"
#include "types.h"

#include <QPoint>
#include <memory>
#include <xcb/xcb.h>

namespace KWin::xwl
{

/**
 * Interface class for all data sharing in the context of X selections
 * and Wayland's internal mechanism.
 *
 * Exists only once per Xwayland session.
 */
template<typename Window>
class data_bridge
{
public:
    data_bridge(runtime const& core)
        : core{core}
    {
        xcb_prefetch_extension_data(core.x11.connection, &xcb_xfixes_id);
        xfixes = xcb_get_extension_data(core.x11.connection, &xcb_xfixes_id);

        clipboard.reset(new xwl::clipboard(core));
        dnd.reset(new drag_and_drop<Toplevel>(core));
        primary_selection.reset(new xwl::primary_selection(core));
    }

    bool filter_event(xcb_generic_event_t* event)
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
            return handle_xfixes_notify(
                reinterpret_cast<xcb_xfixes_selection_notify_event_t*>(event));
        }
        return false;
    }

    drag_event_reply drag_move_filter(Window* target, QPoint const& pos)
    {
        if (!dnd) {
            return drag_event_reply::wayland;
        }
        return dnd->drag_move_filter(target, pos);
    }

private:
    bool handle_xfixes_notify(xcb_xfixes_selection_notify_event_t* event)
    {
        if (event->selection == core.space->atoms->clipboard) {
            return xwl::handle_xfixes_notify(clipboard.get(), event);
        }
        if (event->selection == core.space->atoms->primary_selection) {
            return xwl::handle_xfixes_notify(primary_selection.get(), event);
        }
        if (event->selection == core.space->atoms->xdnd_selection) {
            return xwl::handle_xfixes_notify(dnd.get(), event);
        }
        return false;
    }

    xcb_query_extension_reply_t const* xfixes{nullptr};
    runtime const& core;

    std::unique_ptr<xwl::clipboard> clipboard;
    std::unique_ptr<drag_and_drop<Window>> dnd;
    std::unique_ptr<xwl::primary_selection> primary_selection;
};

}
