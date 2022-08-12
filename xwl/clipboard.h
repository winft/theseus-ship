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

#include "selection_data.h"

#include <Wrapland/Server/data_source.h>

struct xcb_xfixes_selection_notify_event_t;

namespace KWin::xwl
{
class clipboard;
class data_source_ext;

/**
 * Represents the X clipboard, which is on Wayland side just called
 * @e selection.
 */
class KWIN_EXPORT clipboard
{
public:
    selection_data<Wrapland::Server::data_source, data_source_ext> data;

    clipboard(runtime const& core);

    Wrapland::Server::data_source* get_current_source() const;
    void set_selection(Wrapland::Server::data_source* source) const;

    void handle_x11_offer_change(std::vector<std::string> const& added,
                                 std::vector<std::string> const& removed);
    bool handle_client_message(xcb_client_message_event_t* event);
    void do_handle_xfixes_notify(xcb_xfixes_selection_notify_event_t* event);

private:
    Q_DISABLE_COPY(clipboard)
};

}
