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

#include "selection_source.h"
#include "transfer.h"
#include "xwayland.h"

#include "wayland_server.h"
#include "workspace.h"

#include "win/x11/window.h"

#include <Wrapland/Client/datadevice.h>
#include <Wrapland/Client/datasource.h>

#include <Wrapland/Server/data_device.h>
#include <Wrapland/Server/data_source.h>
#include <Wrapland/Server/seat.h>

#include <xcb/xcb_event.h>
#include <xcb/xfixes.h>

#include <xwayland_logging.h>

namespace KWin
{
namespace Xwl
{

Clipboard::Clipboard(xcb_atom_t atom,
                     srv_data_device* srv_dev,
                     clt_data_device* clt_dev,
                     x11_data const& x11)
{
    data = create_selection_data(atom, srv_dev, clt_dev, x11);

    register_x11_selection(this, QSize(10, 10));

    QObject::connect(waylandServer()->seat(),
                     &Wrapland::Server::Seat::selectionChanged,
                     data.qobject.get(),
                     [this] { handle_wl_selection_change(this); });

    QObject::connect(data.srv_device,
                     &srv_data_device::selection_changed,
                     data.qobject.get(),
                     [this, dev = data.srv_device] { get_selection_setter()(dev->selection()); });
}

Clipboard::srv_data_source* Clipboard::get_current_source() const
{
    return waylandServer()->seat()->selection();
}

Wrapland::Client::DataDeviceManager* Clipboard::get_internal_device_manager() const
{
    return waylandServer()->internalDataDeviceManager();
}

std::function<void(Clipboard::srv_data_source*)> Clipboard::get_selection_setter() const
{
    return [](srv_data_source* src) { waylandServer()->seat()->setSelection(src); };
}

} // namespace Xwl
} // namespace KWin
