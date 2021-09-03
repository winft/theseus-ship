/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
Copyright (C) 2018 Roman Gilg <subdiff@gmail.com>

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
#include "touch_redirect.h"

#include "event_filter.h"
#include "event_spy.h"
#include "touch.h"
#include <abstract_wayland_output.h>
#include <config-kwin.h>
#include <decorations/decoratedclient.h>
#include <platform.h>
#include <toplevel.h>
#include <wayland_server.h>
#include <win/input.h>
#include <workspace.h>

#include <KDecoration2/Decoration>

#include <Wrapland/Server/drag_pool.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/touch_pool.h>

#include <KScreenLocker/KsldApp>

#include <QHoverEvent>
#include <QWindow>

namespace KWin::input
{

touch_redirect::touch_redirect()
    : device_redirect()
{
}

touch_redirect::~touch_redirect() = default;

void touch_redirect::init()
{
    Q_ASSERT(!inited());
    setInited(true);
    device_redirect::init();

    if (waylandServer()->hasScreenLockerIntegration()) {
        connect(
            ScreenLocker::KSldApp::self(), &ScreenLocker::KSldApp::lockStateChanged, this, [this] {
                if (!inited() || !waylandServer()->seat()->hasTouch()) {
                    return;
                }
                cancel();
                // position doesn't matter
                update();
            });
    }
    connect(workspace(), &QObject::destroyed, this, [this] { setInited(false); });
    connect(waylandServer(), &QObject::destroyed, this, [this] { setInited(false); });
}

bool touch_redirect::focusUpdatesBlocked()
{
    if (!inited()) {
        return true;
    }
    if (m_windowUpdatedInCycle) {
        return true;
    }
    m_windowUpdatedInCycle = true;
    if (waylandServer()->seat()->drags().is_touch_drag()) {
        return true;
    }
    if (m_touches > 1) {
        // first touch defines focus
        return true;
    }
    return false;
}

bool touch_redirect::positionValid() const
{
    Q_ASSERT(m_touches >= 0);
    // we can only determine a position with at least one touch point
    return m_touches;
}

void touch_redirect::focusUpdate(Toplevel* focusOld, Toplevel* focusNow)
{
    // TODO: handle pointer grab aka popups

    if (focusOld && focusOld->control) {
        win::leave_event(focusOld);
    }
    disconnect(m_focusGeometryConnection);
    m_focusGeometryConnection = QMetaObject::Connection();

    if (focusNow && focusNow->control) {
        win::enter_event(focusNow, m_lastPosition.toPoint());
        workspace()->updateFocusMousePosition(m_lastPosition.toPoint());
    }

    auto seat = waylandServer()->seat();
    if (!focusNow || !focusNow->surface() || decoration()) {
        // no new surface or internal window or on decoration -> cleanup
        seat->touches().set_focused_surface(nullptr);
        return;
    }

    // TODO: invalidate pointer focus?

    // FIXME: add input transformation API to Wrapland::Server::Seat for touch input
    seat->touches().set_focused_surface(focusNow->surface(),
                                        -1 * focusNow->input_transform().map(focusNow->pos())
                                            + focusNow->pos());
    m_focusGeometryConnection = connect(focusNow, &Toplevel::frame_geometry_changed, this, [this] {
        if (!focus()) {
            return;
        }
        auto seat = waylandServer()->seat();
        if (focus()->surface() != seat->touches().get_focus().surface) {
            return;
        }
        seat->touches().set_focused_surface_position(
            -1 * focus()->input_transform().map(focus()->pos()) + focus()->pos());
    });
}

void touch_redirect::cleanupInternalWindow(QWindow* old, QWindow* now)
{
    Q_UNUSED(old);
    Q_UNUSED(now);

    // nothing to do
}

void touch_redirect::cleanupDecoration(Decoration::DecoratedClientImpl* old,
                                       Decoration::DecoratedClientImpl* now)
{
    Q_UNUSED(old);
    Q_UNUSED(now);

    // nothing to do
}

void touch_redirect::insertId(qint32 internalId, qint32 wraplandId)
{
    m_idMapper.insert(internalId, wraplandId);
}

qint32 touch_redirect::mappedId(qint32 internalId)
{
    auto it = m_idMapper.constFind(internalId);
    if (it != m_idMapper.constEnd()) {
        return it.value();
    }
    return -1;
}

void touch_redirect::removeId(qint32 internalId)
{
    m_idMapper.remove(internalId);
}

QPointF get_abs_pos(QPointF const& pos, touch* dev)
{
    auto out = dev->output;
    if (!out) {
        auto const& outs = kwinApp()->platform->enabledOutputs();
        if (outs.empty()) {
            return QPointF();
        }
        out = static_cast<AbstractWaylandOutput*>(outs.front());
    }
    auto const& geo = out->geometry();
    return QPointF(geo.x() + geo.width() * pos.x(), geo.y() + geo.height() * pos.y());
};

void touch_redirect::process_down(touch_down_event const& event)
{
    auto const pos = get_abs_pos(event.pos, event.base.dev);
    processDown(event.id, pos, event.base.time_msec, event.base.dev);
#if !HAVE_WLR_TOUCH_FRAME
    frame();
#endif
}

void touch_redirect::processDown(qint32 id, const QPointF& pos, quint32 time, input::touch* device)
{
    Q_UNUSED(device)
    if (!inited()) {
        return;
    }
    m_lastPosition = pos;
    m_windowUpdatedInCycle = false;
    m_touches++;
    if (m_touches == 1) {
        update();
    }
    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::touchDown, std::placeholders::_1, id, pos, time));
    kwinApp()->input->redirect->processFilters(
        std::bind(&input::event_filter::touchDown, std::placeholders::_1, id, pos, time));
    m_windowUpdatedInCycle = false;
}

void touch_redirect::process_up(touch_up_event const& event)
{
    processUp(event.id, event.base.time_msec, event.base.dev);
#if !HAVE_WLR_TOUCH_FRAME
    frame();
#endif
}

void touch_redirect::processUp(qint32 id, quint32 time, input::touch* device)
{
    Q_UNUSED(device)
    if (!inited()) {
        return;
    }
    m_windowUpdatedInCycle = false;
    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::touchUp, std::placeholders::_1, id, time));
    kwinApp()->input->redirect->processFilters(
        std::bind(&input::event_filter::touchUp, std::placeholders::_1, id, time));
    m_windowUpdatedInCycle = false;
    m_touches--;
    if (m_touches == 0) {
        update();
    }
}

void touch_redirect::process_motion(touch_motion_event const& event)
{
    auto const pos = get_abs_pos(event.pos, event.base.dev);
    processMotion(event.id, pos, event.base.time_msec, event.base.dev);
#if !HAVE_WLR_TOUCH_FRAME
    frame();
#endif
}

void touch_redirect::processMotion(qint32 id,
                                   const QPointF& pos,
                                   quint32 time,
                                   input::touch* device)
{
    Q_UNUSED(device)
    if (!inited()) {
        return;
    }
    m_lastPosition = pos;
    m_windowUpdatedInCycle = false;
    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::touchMotion, std::placeholders::_1, id, pos, time));
    kwinApp()->input->redirect->processFilters(
        std::bind(&input::event_filter::touchMotion, std::placeholders::_1, id, pos, time));
    m_windowUpdatedInCycle = false;
}

void touch_redirect::cancel()
{
    if (!inited()) {
        return;
    }
    waylandServer()->seat()->touches().cancel_sequence();
    m_idMapper.clear();
}

void touch_redirect::frame()
{
    if (!inited()) {
        return;
    }
    waylandServer()->seat()->touches().touch_frame();
}

}
