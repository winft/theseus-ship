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

#include "drag.h"
#include "selection.h"

#include <Wrapland/Server/data_source.h>

#include <QPoint>
#include <memory>
#include <vector>

namespace KWin
{
class Toplevel;

namespace Xwl
{
class Dnd;
enum class DragEventReply;

template<>
void do_handle_xfixes_notify(Dnd* sel, xcb_xfixes_selection_notify_event_t* event);
template<>
bool handle_client_message(Dnd* sel, xcb_client_message_event_t* event);
template<>
void handle_x11_offer_change(Dnd* sel, QStringList const& added, QStringList const& removed);

/**
 * Represents the drag and drop mechanism, on X side this is the XDND protocol.
 * For more information on XDND see: https://johnlindal.wixsite.com/xdnd
 */
class Dnd
{
public:
    selection_data<Wrapland::Server::data_source, data_source_ext> data;

    // active drag or null when no drag active
    std::unique_ptr<Drag> m_currentDrag;
    std::vector<std::unique_ptr<Drag>> m_oldDrags;

    Dnd(xcb_atom_t atom, x11_data const& x11);

    static uint32_t version();

    DragEventReply drag_move_filter(Toplevel* target, QPoint const& pos);

private:
    // start and end Wl native client drags (Wl -> Xwl)
    void start_drag();
    void end_drag();
    void clear_old_drag(Drag* drag);

    Q_DISABLE_COPY(Dnd)
};

}
}
