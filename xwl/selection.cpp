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
#include "selection.h"
#include "databridge.h"

#include "atoms.h"

namespace KWin
{
namespace Xwl
{

Selection::Selection(xcb_atom_t atom)
    : m_qobject(new q_selection)
    , m_atom(atom)
{
    xcb_connection_t* xcbConn = kwinApp()->x11Connection();
    m_window = xcb_generate_id(kwinApp()->x11Connection());
    m_requestorWindow = m_window;
    xcb_flush(xcbConn);
}

Selection::~Selection()
{
    delete m_waylandSource;
    delete m_xSource;
    m_waylandSource = nullptr;
    m_xSource = nullptr;
}

} // namespace Xwl
} // namespace KWin
