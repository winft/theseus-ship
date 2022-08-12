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
#include "types.h"

#include <QPoint>
#include <memory>
#include <vector>

namespace KWin
{
class Toplevel;

namespace xwl
{
class data_source_ext;
enum class drag_event_reply;

class wl_visit;

template<typename>
class x11_source;
using x11_source_ext = x11_source<data_source_ext>;

class x11_drag : public drag
{
    Q_OBJECT

public:
    explicit x11_drag(x11_source_ext& source);
    ~x11_drag() override;

    drag_event_reply move_filter(Toplevel* target, QPoint const& pos) override;
    bool handle_client_message(xcb_client_message_event_t* event) override;
    bool end() override;

    void handle_transfer_finished(xcb_timestamp_t time);

    std::unique_ptr<data_source_ext> data_source;
    std::unique_ptr<wl_visit> visit;

private:
    void set_offers(mime_atoms const& offers);
    void set_drag_target();

    bool check_for_finished();

    x11_source_ext& source;
    mime_atoms offers;
    std::vector<std::pair<xcb_timestamp_t, bool>> data_requests;

    std::vector<std::unique_ptr<wl_visit>> old_visits;

    Q_DISABLE_COPY(x11_drag)
};

}
}
