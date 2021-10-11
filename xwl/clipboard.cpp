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

#include "wayland_server.h"

#include <Wrapland/Server/seat.h>

namespace KWin::Xwl
{

Clipboard::Clipboard(xcb_atom_t atom, x11_data const& x11)
{
    data = create_selection_data<Wrapland::Server::data_source, data_source_ext>(atom, x11);

    register_x11_selection(this, QSize(10, 10));

    QObject::connect(waylandServer()->seat(),
                     &Wrapland::Server::Seat::selectionChanged,
                     data.qobject.get(),
                     [this] { handle_wl_selection_change(this); });
}

Wrapland::Server::data_source* Clipboard::get_current_source() const
{
    return waylandServer()->seat()->selection();
}

std::function<void(Wrapland::Server::data_source*)> Clipboard::get_selection_setter() const
{
    return [](Wrapland::Server::data_source* src) { waylandServer()->seat()->setSelection(src); };
}

}
