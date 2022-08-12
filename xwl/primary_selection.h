/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "selection_data.h"

#include <Wrapland/Server/primary_selection.h>

struct xcb_xfixes_selection_notify_event_t;

namespace KWin::xwl
{
class primary_selection_source_ext;

class primary_selection
{
public:
    selection_data<Wrapland::Server::primary_selection_source, primary_selection_source_ext> data;

    primary_selection(x11_data const& x11);

    Wrapland::Server::primary_selection_source* get_current_source() const;
    void set_selection(Wrapland::Server::primary_selection_source* source) const;

    void handle_x11_offer_change(std::vector<std::string> const& added,
                                 std::vector<std::string> const& removed);
    bool handle_client_message(xcb_client_message_event_t* event);
    void do_handle_xfixes_notify(xcb_xfixes_selection_notify_event_t* event);

private:
    Q_DISABLE_COPY(primary_selection)
};

}
