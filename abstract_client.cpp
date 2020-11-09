/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>
Copyright (C) 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

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
#include "abstract_client.h"

#include "cursor.h"
#include "focuschain.h"
#include "outline.h"
#include "screens.h"
#include "screenedge.h"
#include "useractions.h"
#include "win/control.h"
#include "win/setup.h"
#include "win/win.h"
#include "workspace.h"

#include "wayland_server.h"

namespace KWin
{

AbstractClient::AbstractClient()
    : Toplevel()
{
    win::setup_connections(this);
}

AbstractClient::~AbstractClient()
{
}

}
