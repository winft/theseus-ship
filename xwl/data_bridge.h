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

#include "types.h"

#include "kwin_export.h"

#include <QPoint>
#include <memory>
#include <xcb/xcb.h>

struct xcb_xfixes_selection_notify_event_t;

namespace KWin
{
class Toplevel;

namespace xwl
{
class clipboard;
template<typename Window>
class drag_and_drop;
enum class drag_event_reply;
class primary_selection;

/**
 * Interface class for all data sharing in the context of X selections
 * and Wayland's internal mechanism.
 *
 * Exists only once per Xwayland session.
 */
class KWIN_EXPORT data_bridge
{
public:
    data_bridge(x11_data const& x11);
    ~data_bridge();

    bool filter_event(xcb_generic_event_t* event);
    drag_event_reply drag_move_filter(Toplevel* target, QPoint const& pos);

private:
    bool handle_xfixes_notify(xcb_xfixes_selection_notify_event_t* event);

    xcb_query_extension_reply_t const* xfixes{nullptr};
    x11_data const& x11;

    std::unique_ptr<xwl::clipboard> clipboard;
    std::unique_ptr<drag_and_drop<Toplevel>> dnd;
    std::unique_ptr<xwl::primary_selection> primary_selection;

    Q_DISABLE_COPY(data_bridge)
};

}
}
