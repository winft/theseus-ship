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
#include "selection_x11.h"

#include <QPoint>
#include <Wrapland/Server/data_source.h>
#include <memory>
#include <vector>

namespace KWin
{
class Toplevel;

namespace xwl
{

class data_source_ext;
enum class drag_event_reply;

template<typename Window>
class drag;
template<typename Window>
class wl_drag;
template<typename Window>
class x11_drag;

/**
 * Represents the drag and drop mechanism, on X side this is the XDND protocol.
 * For more information on XDND see: https://johnlindal.wixsite.com/xdnd
 */
class drag_and_drop
{
public:
    selection_data<Wrapland::Server::data_source, data_source_ext> data;

    std::unique_ptr<wl_drag<Toplevel>> wldrag;
    std::unique_ptr<x11_drag<Toplevel>> xdrag;
    std::vector<std::unique_ptr<drag<Toplevel>>> old_drags;

    drag_and_drop(x11_data const& x11);
    ~drag_and_drop();

    static uint32_t version();

    drag_event_reply drag_move_filter(Toplevel* target, QPoint const& pos);

    void handle_x11_offer_change(std::vector<std::string> const& added,
                                 std::vector<std::string> const& removed);
    bool handle_client_message(xcb_client_message_event_t* event);
    void do_handle_xfixes_notify(xcb_xfixes_selection_notify_event_t* event);

private:
    // start and end Wl native client drags (Wl -> Xwl)
    void start_drag();
    void end_drag();
    void clear_old_drag(xwl::drag<Toplevel>* drag);

    Q_DISABLE_COPY(drag_and_drop)
};

}
}
