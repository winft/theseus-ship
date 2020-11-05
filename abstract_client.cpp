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

bool AbstractClient::performMouseCommand(Options::MouseCommand cmd, const QPoint &globalPos)
{
    return win::perform_mouse_command(this, cmd, globalPos);
}

QList< AbstractClient* > AbstractClient::mainClients() const
{
    if (auto t = dynamic_cast<const AbstractClient *>(control()->transient_lead())) {
        return QList<AbstractClient*>{const_cast< AbstractClient* >(t)};
    }
    return QList<AbstractClient*>();
}

void AbstractClient::leaveMoveResize()
{
    workspace()->setMoveResizeClient(nullptr);
    control()->move_resize().enabled = false;
    if (ScreenEdges::self()->isDesktopSwitchingMovingClients())
        ScreenEdges::self()->reserveDesktopSwitching(false, Qt::Vertical|Qt::Horizontal);
    if (control()->electric_maximizing()) {
        outline()->hide();
        win::elevate(this, false);
    }
}

QRect AbstractClient::iconGeometry() const
{
    auto management = control()->wayland_management();
    if (!management || !waylandServer()) {
        // window management interface is only available if the surface is mapped
        return QRect();
    }

    int minDistance = INT_MAX;
    AbstractClient *candidatePanel = nullptr;
    QRect candidateGeom;

    for (auto i = management->minimizedGeometries().constBegin(),
         end = management->minimizedGeometries().constEnd(); i != end; ++i) {
        AbstractClient *client = waylandServer()->findAbstractClient(i.key());
        if (!client) {
            continue;
        }
        const int distance = QPoint(client->pos() - pos()).manhattanLength();
        if (distance < minDistance) {
            minDistance = distance;
            candidatePanel = client;
            candidateGeom = i.value();
        }
    }
    if (!candidatePanel) {
        return QRect();
    }
    return candidateGeom.translated(candidatePanel->pos());
}

}
