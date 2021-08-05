/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "primary_selection.h"

#include <Wrapland/Server/seat.h>

namespace KWin::Xwl
{

primary_selection::primary_selection(xcb_atom_t atom,
                                     srv_data_device* srv_dev,
                                     clt_data_device* clt_dev)
{
    data = create_selection_data(atom, srv_dev, clt_dev);

    register_x11_selection(this, QSize(10, 10));

    QObject::connect(waylandServer()->seat(),
                     &Wrapland::Server::Seat::primarySelectionChanged,
                     data.qobject.get(),
                     [this] { handle_wl_selection_change(this); });
}

primary_selection::srv_data_device* primary_selection::get_current_device() const
{
    return waylandServer()->seat()->primarySelection();
}

Wrapland::Client::PrimarySelectionDeviceManager*
primary_selection::get_internal_device_manager() const
{
    return waylandServer()->internalPrimarySelectionDeviceManager();
}

std::function<void(primary_selection::srv_data_device*)>
primary_selection::get_selection_setter() const
{
    return [](srv_data_device* dev) { waylandServer()->seat()->setPrimarySelection(dev); };
}

}
