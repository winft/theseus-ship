/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "selection.h"

#include <Wrapland/Server/primary_selection.h>

#include <functional>

namespace KWin::Xwl
{

class primary_selection
{
public:
    using srv_data_source = Wrapland::Server::primary_selection_source;
    using internal_data_source = primary_selection_source_ext;

    selection_data<srv_data_source, internal_data_source> data;
    QMetaObject::Connection source_check_connection;

    primary_selection(xcb_atom_t atom, x11_data const& x11);

    srv_data_source* get_current_source() const;
    std::function<void(srv_data_source*)> get_selection_setter() const;

private:
    Q_DISABLE_COPY(primary_selection)
};

}
