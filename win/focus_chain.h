/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2012 Martin Gräßlin <mgraesslin@kde.org>

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

#include "kwin_export.h"

#include "types.h"

#include <QHash>

namespace KWin
{

namespace base
{
class output;
}

class Toplevel;

namespace win
{

class space;

using focus_chain_list = QList<Toplevel*>;

/**
 * @brief Data struct to handle the various focus chains.
 *
 * A focus chain is a list of Clients containing information on which Client should be activated.
 *
 * This focus_chain holds multiple independent chains. There is one chain of most recently used
 * Clients which is primarily used by TabBox to build up the list of Clients for navigation. The
 * chains are organized as a normal QList of Clients with the most recently used Client being the
 * last item of the list, that is a LIFO like structure.
 *
 * In addition there is one chain for each virtual desktop which is used to determine which Client
 * should get activated when the user switches to another virtual desktop.
 */
class focus_chain
{
public:
    focus_chain(win::space& space)
        : space{space}
    {
    }

    struct {
        focus_chain_list latest_use;
        QHash<unsigned int, focus_chain_list> desktops;
    } chains;

    Toplevel* active_window{nullptr};
    unsigned int current_desktop{0};

    bool has_separate_screen_focus{false};
    win::space& space;
};

}
}
