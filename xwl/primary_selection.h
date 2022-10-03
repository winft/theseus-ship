/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "event_x11.h"
#include "selection_data.h"
#include "selection_wl.h"
#include "selection_x11.h"
#include "sources_ext.h"

#include "base/wayland/server.h"

#include <Wrapland/Server/primary_selection.h>
#include <Wrapland/Server/seat.h>

namespace KWin::xwl
{

template<typename Space>
class primary_selection
{
public:
    using space_t = Space;

    selection_data<Space, Wrapland::Server::primary_selection_source, primary_selection_source_ext>
        data;

    primary_selection(runtime<Space> const& core)
    {
        data = create_selection_data<Space,
                                     Wrapland::Server::primary_selection_source,
                                     primary_selection_source_ext>(
            core.x11.atoms->primary_selection, core);

        register_x11_selection(this, QSize(10, 10));

        QObject::connect(waylandServer()->seat(),
                         &Wrapland::Server::Seat::primarySelectionChanged,
                         data.qobject.get(),
                         [this] { handle_wl_selection_change(this); });
    }

    Wrapland::Server::primary_selection_source* get_current_source() const
    {
        return waylandServer()->seat()->primarySelection();
    }

    void set_selection(Wrapland::Server::primary_selection_source* source) const
    {
        waylandServer()->seat()->setPrimarySelection(source);
    }

    void handle_x11_offer_change(std::vector<std::string> const& added,
                                 std::vector<std::string> const& removed)
    {
        xwl::handle_x11_offer_change(this, added, removed);
    }
};

}
