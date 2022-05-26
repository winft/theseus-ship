/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

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
#include "screen_edge_item.h"

#include "config-kwin.h"

#include "win/screen_edges.h"
#include "win/singleton_interface.h"
#include "win/space.h"

#include <QAction>

namespace KWin::scripting
{

screen_edge_item::screen_edge_item(QObject* parent)
    : QObject(parent)
    , m_enabled(true)
    , m_edge(NoEdge)
    , m_action(new QAction(this))
{
    connect(m_action, &QAction::triggered, this, &screen_edge_item::activated);
}

screen_edge_item::~screen_edge_item()
{
}

void screen_edge_item::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    disableEdge();
    m_enabled = enabled;
    enableEdge();
    Q_EMIT enabledChanged();
}

void screen_edge_item::setEdge(Edge edge)
{
    if (m_edge == edge) {
        return;
    }
    disableEdge();
    m_edge = edge;
    enableEdge();
    Q_EMIT edgeChanged();
}

void screen_edge_item::enableEdge()
{
    if (!m_enabled || m_edge == NoEdge) {
        return;
    }
    switch (m_mode) {
    case Mode::Pointer:
        win::singleton_interface::space->edges->reserve(
            static_cast<ElectricBorder>(m_edge), this, "borderActivated");
        break;
    case Mode::Touch:
        win::singleton_interface::space->edges->reserveTouch(static_cast<ElectricBorder>(m_edge),
                                                             m_action);
        break;
    default:
        Q_UNREACHABLE();
    }
}

void screen_edge_item::disableEdge()
{
    if (!m_enabled || m_edge == NoEdge) {
        return;
    }
    switch (m_mode) {
    case Mode::Pointer:
        win::singleton_interface::space->edges->unreserve(static_cast<ElectricBorder>(m_edge),
                                                          this);
        break;
    case Mode::Touch:
        win::singleton_interface::space->edges->unreserveTouch(static_cast<ElectricBorder>(m_edge),
                                                               m_action);
        break;
    default:
        Q_UNREACHABLE();
    }
}

bool screen_edge_item::borderActivated(ElectricBorder edge)
{
    if (edge != static_cast<ElectricBorder>(m_edge) || !m_enabled) {
        return false;
    }
    Q_EMIT activated();
    return true;
}

void screen_edge_item::setMode(Mode mode)
{
    if (m_mode == mode) {
        return;
    }
    disableEdge();
    m_mode = mode;
    enableEdge();
    Q_EMIT modeChanged();
}

}
