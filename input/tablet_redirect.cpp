/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2019 Aleix Pol Gonzalez <aleixpol@kde.org>

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
#include "tablet_redirect.h"

#include "decorations/decoratedclient.h"
#include "device_redirect.h"
#include "event_filter.h"
#include "input_event_spy.h"
#include "pointer_redirect.h"
#include "toplevel.h"
#include "wayland_server.h"
#include "workspace.h"
// KDecoration
#include <KDecoration2/Decoration>
// Wrapland
#include <Wrapland/Server/seat.h>
// screenlocker
#include <KScreenLocker/KsldApp>
// Qt
#include <QHoverEvent>
#include <QWindow>

namespace KWin::input
{
tablet_redirect::tablet_redirect(redirect* parent)
    : device_redirect(parent)
{
}

tablet_redirect::~tablet_redirect() = default;

void tablet_redirect::init()
{
    Q_ASSERT(!inited());
    setInited(true);
    device_redirect::init();

    connect(workspace(), &QObject::destroyed, this, [this] { setInited(false); });
    connect(waylandServer(), &QObject::destroyed, this, [this] { setInited(false); });
}

void tablet_redirect::tabletToolEvent(redirect::TabletEventType type,
                                      const QPointF& pos,
                                      qreal pressure,
                                      int xTilt,
                                      int yTilt,
                                      qreal rotation,
                                      bool tipDown,
                                      bool tipNear,
                                      quint64 serialId,
                                      quint64 toolId,
                                      void* device)
{
    Q_UNUSED(device)
    Q_UNUSED(toolId)
    if (!inited()) {
        return;
    }
    m_lastPosition = pos;

    QEvent::Type t;
    switch (type) {
    case redirect::Axis:
        t = QEvent::TabletMove;
        break;
    case redirect::Tip:
        t = tipDown ? QEvent::TabletPress : QEvent::TabletRelease;
        break;
    case redirect::Proximity:
        t = tipNear ? QEvent::TabletEnterProximity : QEvent::TabletLeaveProximity;
        break;
    }

    const auto button = m_tipDown ? Qt::LeftButton : Qt::NoButton;
    QTabletEvent ev(t,
                    pos,
                    pos,
                    QTabletEvent::Stylus,
                    QTabletEvent::Pen,
                    pressure,
                    xTilt,
                    yTilt,
                    0, // tangentialPressure
                    rotation,
                    0, // z
                    Qt::NoModifier,
                    serialId,
                    button,
                    button);

    kwinApp()->input_redirect->processSpies(
        std::bind(&InputEventSpy::tabletToolEvent, std::placeholders::_1, &ev));
    kwinApp()->input_redirect->processFilters(
        std::bind(&input::event_filter::tabletToolEvent, std::placeholders::_1, &ev));

    m_tipDown = tipDown;
    m_tipNear = tipNear;
}

void tablet_redirect::tabletToolButtonEvent(uint button, bool isPressed)
{
    if (isPressed)
        m_toolPressedButtons.insert(button);
    else
        m_toolPressedButtons.remove(button);

    kwinApp()->input_redirect->processSpies(std::bind(
        &InputEventSpy::tabletToolButtonEvent, std::placeholders::_1, m_toolPressedButtons));
    kwinApp()->input_redirect->processFilters(std::bind(
        &input::event_filter::tabletToolButtonEvent, std::placeholders::_1, m_toolPressedButtons));
}

void tablet_redirect::tabletPadButtonEvent(uint button, bool isPressed)
{
    if (isPressed) {
        m_padPressedButtons.insert(button);
    } else {
        m_padPressedButtons.remove(button);
    }

    kwinApp()->input_redirect->processSpies(std::bind(
        &InputEventSpy::tabletPadButtonEvent, std::placeholders::_1, m_padPressedButtons));
    kwinApp()->input_redirect->processFilters(std::bind(
        &input::event_filter::tabletPadButtonEvent, std::placeholders::_1, m_padPressedButtons));
}

void tablet_redirect::tabletPadStripEvent(int number, int position, bool isFinger)
{
    kwinApp()->input_redirect->processSpies(std::bind(
        &InputEventSpy::tabletPadStripEvent, std::placeholders::_1, number, position, isFinger));
    kwinApp()->input_redirect->processFilters(std::bind(&input::event_filter::tabletPadStripEvent,
                                                        std::placeholders::_1,
                                                        number,
                                                        position,
                                                        isFinger));
}

void tablet_redirect::tabletPadRingEvent(int number, int position, bool isFinger)
{
    kwinApp()->input_redirect->processSpies(std::bind(
        &InputEventSpy::tabletPadRingEvent, std::placeholders::_1, number, position, isFinger));
    kwinApp()->input_redirect->processFilters(std::bind(&input::event_filter::tabletPadRingEvent,
                                                        std::placeholders::_1,
                                                        number,
                                                        position,
                                                        isFinger));
}

void tablet_redirect::cleanupDecoration(Decoration::DecoratedClientImpl* old,
                                        Decoration::DecoratedClientImpl* now)
{
    Q_UNUSED(old)
    Q_UNUSED(now)
}

void tablet_redirect::cleanupInternalWindow(QWindow* old, QWindow* now)
{
    Q_UNUSED(old)
    Q_UNUSED(now)
}

void tablet_redirect::focusUpdate(KWin::Toplevel* old, KWin::Toplevel* now)
{
    Q_UNUSED(old)
    Q_UNUSED(now)
}

}
