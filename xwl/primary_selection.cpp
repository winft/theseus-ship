/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "primary_selection.h"

#include "selection_wl.h"
#include "selection_x11.h"
#include "sources_ext.h"

#include "base/wayland/server.h"

#include <Wrapland/Server/seat.h>

namespace KWin::xwl
{

primary_selection::primary_selection(x11_data const& x11)
{
    data = create_selection_data<Wrapland::Server::primary_selection_source,
                                 primary_selection_source_ext>(x11.space->atoms->primary_selection,
                                                               x11);

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

void primary_selection::set_selection(Wrapland::Server::primary_selection_source* source) const
{
    waylandServer()->seat()->setPrimarySelection(source);
}

}
