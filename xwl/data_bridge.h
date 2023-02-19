/*
SPDX-FileCopyrightText: 2018 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
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
template<typename Space>
class data_bridge
{
public:
    data_bridge(runtime<Space> const& core)
        : core{core}
    {
        xcb_prefetch_extension_data(core.x11.connection, &xcb_xfixes_id);
        xfixes = xcb_get_extension_data(core.x11.connection, &xcb_xfixes_id);

        clipboard = std::make_unique<xwl::clipboard<Space>>(core);
        dnd = std::make_unique<xwl::drag_and_drop<Space>>(core);
        primary_selection = std::make_unique<xwl::primary_selection<Space>>(core);
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

    drag_event_reply drag_move_filter(std::optional<typename Space::window_t> target,
                                      QPoint const& pos)
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
    runtime<Space> const& core;

    std::unique_ptr<xwl::clipboard<Space>> clipboard;
    std::unique_ptr<drag_and_drop<Space>> dnd;
    std::unique_ptr<xwl::primary_selection<Space>> primary_selection;
};

}
