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
#include "databridge.h"

#include "clipboard.h"
#include "dnd.h"
#include "primary_selection.h"

#include "atoms.h"

namespace KWin::xwl
{

data_bridge::data_bridge(x11_data const& x11)
    : QObject()
{
    xcb_prefetch_extension_data(x11.connection, &xcb_xfixes_id);
    xfixes = xcb_get_extension_data(x11.connection, &xcb_xfixes_id);

    m_clipboard.reset(new clipboard(atoms->clipboard, x11));
    m_dnd.reset(new drag_and_drop(atoms->xdnd_selection, x11));
    m_primarySelection.reset(new primary_selection(atoms->primary_selection, x11));
}

data_bridge::~data_bridge() = default;

bool data_bridge::filter_event(xcb_generic_event_t* event)
{
    if (xwl::filter_event(m_clipboard.get(), event)) {
        return true;
    }
    if (xwl::filter_event(m_dnd.get(), event)) {
        return true;
    }
    if (xwl::filter_event(m_primarySelection.get(), event)) {
        return true;
    }
    if (event->response_type - xfixes->first_event == XCB_XFIXES_SELECTION_NOTIFY) {
        return handle_xfixes_notify((xcb_xfixes_selection_notify_event_t*)event);
    }
    return false;
}

bool data_bridge::handle_xfixes_notify(xcb_xfixes_selection_notify_event_t* event)
{
    if (event->selection == atoms->clipboard) {
        return xwl::handle_xfixes_notify(m_clipboard.get(), event);
    }
    if (event->selection == atoms->primary_selection) {
        return xwl::handle_xfixes_notify(m_primarySelection.get(), event);
    }
    if (event->selection == atoms->xdnd_selection) {
        return xwl::handle_xfixes_notify(m_dnd.get(), event);
    }
    return false;
}

drag_event_reply data_bridge::drag_move_filter(Toplevel* target, QPoint const& pos)
{
    if (!m_dnd) {
        return drag_event_reply::wayland;
    }
    return m_dnd->drag_move_filter(target, pos);
}

}
