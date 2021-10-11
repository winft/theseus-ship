/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "primary_selection.h"

#include "wayland_server.h"

#include <Wrapland/Server/seat.h>

namespace KWin::Xwl
{

primary_selection::primary_selection(xcb_atom_t atom, x11_data const& x11)
{
    data = create_selection_data<Wrapland::Server::primary_selection_source,
                                 primary_selection_source_ext>(atom, x11);

    register_x11_selection(this, QSize(10, 10));

    QObject::connect(waylandServer()->seat(),
                     &Wrapland::Server::Seat::primarySelectionChanged,
                     data.qobject.get(),
                     [this] { handle_wl_selection_change(this); });
}

Wrapland::Server::primary_selection_source* primary_selection::get_current_source() const
{
    return waylandServer()->seat()->primarySelection();
}

std::function<void(Wrapland::Server::primary_selection_source*)>
primary_selection::get_selection_setter() const
{
    return [](Wrapland::Server::primary_selection_source* src) {
        waylandServer()->seat()->setPrimarySelection(src);
    };
}

}
