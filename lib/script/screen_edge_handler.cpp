/*
SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "screen_edge_handler.h"

#include "config-kwin.h"
#include "win/singleton_interface.h"

#include <QAction>

namespace KWin::scripting
{

screen_edge_handler::screen_edge_handler(QObject* parent)
    : QObject(parent)
    , m_enabled(true)
    , m_edge(NoEdge)
    , m_action(new QAction(this))
{
    connect(m_action, &QAction::triggered, this, &screen_edge_handler::activated);
}

screen_edge_handler::~screen_edge_handler()
{
    disableEdge();
}

void screen_edge_handler::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    disableEdge();
    m_enabled = enabled;
    enableEdge();
    Q_EMIT enabledChanged();
}

void screen_edge_handler::setEdge(Edge edge)
{
    if (m_edge == edge) {
        return;
    }
    disableEdge();
    m_edge = edge;
    enableEdge();
    Q_EMIT edgeChanged();
}

void screen_edge_handler::enableEdge()
{
    if (!m_enabled || m_edge == NoEdge) {
        return;
    }
    switch (m_mode) {
    case Mode::Pointer:
        reserved_id = win::singleton_interface::edger->reserve(
            static_cast<win::electric_border>(m_edge),
            [this](win::electric_border eb) { return borderActivated(static_cast<Edge>(eb)); });
        break;
    case Mode::Touch:
        win::singleton_interface::edger->reserve_touch(static_cast<win::electric_border>(m_edge),
                                                       m_action);
        break;
    default:
        Q_UNREACHABLE();
    }
}

void screen_edge_handler::disableEdge()
{
    if (!m_enabled || m_edge == NoEdge) {
        return;
    }

    auto edger = win::singleton_interface::edger;
    if (!edger) {
        // Might be after space went down due to Qt's implicit ownership.
        return;
    }

    switch (m_mode) {
    case Mode::Pointer:
        edger->unreserve(static_cast<win::electric_border>(m_edge), reserved_id);
        reserved_id = 0;
        break;
    case Mode::Touch:
        edger->unreserve_touch(static_cast<win::electric_border>(m_edge), m_action);
        break;
    default:
        Q_UNREACHABLE();
    }
}

bool screen_edge_handler::borderActivated(Edge edge)
{
    if (edge != m_edge || !m_enabled) {
        return false;
    }
    Q_EMIT activated();
    return true;
}

void screen_edge_handler::setMode(Mode mode)
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
