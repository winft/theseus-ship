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
#pragma once

#include "event_x11.h"
#include "selection_data.h"
#include "selection_wl.h"
#include "selection_x11.h"
#include "sources_ext.h"

#include "base/wayland/server.h"

#include <Wrapland/Server/data_source.h>
#include <Wrapland/Server/seat.h>

namespace KWin::xwl
{

/**
 * Represents the X clipboard, which is on Wayland side just called
 * @e selection.
 */
template<typename Space>
class clipboard
{
public:
    using space_t = Space;

    selection_data<Space, Wrapland::Server::data_source, data_source_ext> data;

    clipboard(runtime<Space> const& core)
    {
        data = create_selection_data<Space, Wrapland::Server::data_source, data_source_ext>(
            core.space->atoms->clipboard, core);

        register_x11_selection(this, QSize(10, 10));

        QObject::connect(waylandServer()->seat(),
                         &Wrapland::Server::Seat::selectionChanged,
                         data.qobject.get(),
                         [this] { handle_wl_selection_change(this); });
    }

    Wrapland::Server::data_source* get_current_source() const
    {
        return waylandServer()->seat()->selection();
    }

    void set_selection(Wrapland::Server::data_source* source) const
    {
        waylandServer()->seat()->setSelection(source);
    }

    void handle_x11_offer_change(std::vector<std::string> const& added,
                                 std::vector<std::string> const& removed)
    {
        xwl::handle_x11_offer_change(this, added, removed);
    }
};

}
