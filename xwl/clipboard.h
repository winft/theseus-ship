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
    selection_data<Wrapland::Server::data_source, data_source_ext> data;

    Clipboard(xcb_atom_t atom, x11_data const& x11);

    Wrapland::Server::data_source* get_current_source() const;
    void set_selection(Wrapland::Server::data_source* source) const;

private:
    Q_DISABLE_COPY(Clipboard)
};

}
