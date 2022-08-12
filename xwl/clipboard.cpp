/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2019 Roman Gilg <subdiff@gmail.com>

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
#include "clipboard.h"

#include "event_x11.h"
#include "selection_wl.h"
#include "selection_x11.h"
#include "sources_ext.h"

#include "base/wayland/server.h"

#include <Wrapland/Server/seat.h>

namespace KWin::xwl
{

clipboard::clipboard(x11_data const& x11)
{
    data = create_selection_data<Wrapland::Server::data_source, data_source_ext>(
        x11.space->atoms->clipboard, x11);

    register_x11_selection(this, QSize(10, 10));

    QObject::connect(waylandServer()->seat(),
                     &Wrapland::Server::Seat::selectionChanged,
                     data.qobject.get(),
                     [this] { handle_wl_selection_change(this); });
}

Wrapland::Server::data_source* clipboard::get_current_source() const
{
    return waylandServer()->seat()->selection();
}

void clipboard::set_selection(Wrapland::Server::data_source* source) const
{
    waylandServer()->seat()->setSelection(source);
}

void clipboard::handle_x11_offer_change(std::vector<std::string> const& added,
                                        std::vector<std::string> const& removed)
{
    xwl::handle_x11_offer_change(this, added, removed);
}

bool clipboard::handle_client_message(xcb_client_message_event_t* /*event*/)
{
    return false;
}

void clipboard::do_handle_xfixes_notify(xcb_xfixes_selection_notify_event_t* event)
{
    xwl::do_handle_xfixes_notify(this, event);
}

}
