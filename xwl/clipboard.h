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
#ifndef KWIN_XWL_CLIPBOARD
#define KWIN_XWL_CLIPBOARD

#include "selection.h"

#include <Wrapland/Client/datadevice.h>
#include <Wrapland/Client/datasource.h>
#include <Wrapland/Server/data_device.h>
#include <Wrapland/Server/data_source.h>

#include <functional>

namespace KWin::Xwl
{
class Clipboard;

/**
 * Represents the X clipboard, which is on Wayland side just called
 * @e selection.
 */
class Clipboard
{
public:
    using srv_data_device = Wrapland::Server::DataDevice;
    using clt_data_device = Wrapland::Client::DataDevice;
    using srv_data_source = srv_data_device::source_t;
    using clt_source_t = clt_data_device::source_t;

    selection_data<srv_data_device, clt_data_device> data;
    QMetaObject::Connection source_check_connection;

    Clipboard(xcb_atom_t atom,
              srv_data_device* srv_dev,
              clt_data_device* clt_dev,
              x11_data const& x11);

    srv_data_device* get_current_device() const;
    Wrapland::Client::DataDeviceManager* get_internal_device_manager() const;
    std::function<void(srv_data_device*)> get_selection_setter() const;

private:
    Q_DISABLE_COPY(Clipboard)
};

}

#endif
