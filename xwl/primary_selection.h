/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "selection.h"

#include <Wrapland/Client/primary_selection.h>
#include <Wrapland/Server/primary_selection.h>

#include <functional>

namespace KWin::Xwl
{

class primary_selection
{
public:
    using srv_data_device = Wrapland::Server::PrimarySelectionDevice;
    using clt_data_device = Wrapland::Client::PrimarySelectionDevice;
    using srv_data_source = srv_data_device::source_t;
    using clt_source_t = clt_data_device::source_t;

    selection_data<srv_data_device, clt_data_device> data;
    QMetaObject::Connection source_check_connection;

    primary_selection(xcb_atom_t atom,
                      srv_data_device* srv_dev,
                      clt_data_device* clt_dev,
                      x11_data const& x11);

    srv_data_source* get_current_source() const;
    Wrapland::Client::PrimarySelectionDeviceManager* get_internal_device_manager() const;
    std::function<void(srv_data_source*)> get_selection_setter() const;

private:
    Q_DISABLE_COPY(primary_selection)
};

}
