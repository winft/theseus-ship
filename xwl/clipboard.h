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

#include "selection.h"

#include <Wrapland/Server/data_source.h>

#include <functional>

namespace KWin::Xwl
{
class Clipboard;

/**
 * Represents the X clipboard, which is on Wayland side just called
 * @e selection.
 */
class Clipboard
{
public:
    using server_source = Wrapland::Server::data_source;
    using internal_source = data_source_ext;

    selection_data<server_source, internal_source> data;
    QMetaObject::Connection source_check_connection;

    Clipboard(xcb_atom_t atom, x11_data const& x11);

    server_source* get_current_source() const;
    std::function<void(server_source*)> get_selection_setter() const;

private:
    Q_DISABLE_COPY(Clipboard)
};

}
