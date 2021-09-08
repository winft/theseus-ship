/*
    SPDX-FileCopyrightText: 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "pointer_redirect.h"

#include "event.h"
#include "event_filter.h"
#include "event_spy.h"
#include "qt_event.h"
#include "wayland/cursor.h"
#include "wayland/cursor_image.h"

#include <decorations/decoratedclient.h>
#include <effects.h>
#include <platform.h>
#include <screens.h>
#include <wayland_server.h>
#include <win/input.h>
#include <workspace.h>

#include <Wrapland/Server/drag_pool.h>
#include <Wrapland/Server/pointer.h>
#include <Wrapland/Server/pointer_constraints_v1.h>
#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/surface.h>
#include <Wrapland/Server/touch_pool.h>

#include <KDecoration2/Decoration>
#include <KScreenLocker/KsldApp>
#include <QHoverEvent>
#include <QWindow>
#include <linux/input.h>

namespace KWin::input
{

bool pointer_redirect::s_cursorUpdateBlocking{false};

static bool screenContainsPos(const QPointF& pos)
{
    for (int i = 0; i < screens()->count(); ++i) {
        if (screens()->geometry(i).contains(pos.toPoint())) {
            return true;
        }
    }
    return false;
}

static QPointF confineToBoundingBox(const QPointF& pos, const QRectF& boundingBox)
{
    return QPointF(qBound(boundingBox.left(), pos.x(), boundingBox.right() - 1.0),
                   qBound(boundingBox.top(), pos.y(), boundingBox.bottom() - 1.0));
}

pointer_redirect::pointer_redirect()
    : device_redirect()
    , m_supportsWarping(true)
{
}

pointer_redirect::~pointer_redirect() = default;

void pointer_redirect::init()
{
    Q_ASSERT(!inited());
    setInited(true);
    device_redirect::init();

    connect(screens(), &Screens::changed, this, &pointer_redirect::updateAfterScreenChange);
    if (waylandServer()->hasScreenLockerIntegration()) {
        connect(
            ScreenLocker::KSldApp::self(), &ScreenLocker::KSldApp::lockStateChanged, this, [this] {
                waylandServer()->seat()->pointers().cancel_pinch_gesture();
                waylandServer()->seat()->pointers().cancel_swipe_gesture();
                update();
            });
    }
    connect(workspace(), &QObject::destroyed, this, [this] { setInited(false); });
    connect(waylandServer(), &QObject::destroyed, this, [this] { setInited(false); });
    connect(waylandServer()->seat(), &Wrapland::Server::Seat::dragEnded, this, [this] {
        // need to force a focused pointer change
        waylandServer()->seat()->pointers().set_focused_surface(nullptr);
        setFocus(nullptr);
        update();
    });
    // connect the move resize of all window
    auto setupMoveResizeConnection = [this](Toplevel* c) {
        if (!c->control) {
            return;
        }
        connect(c,
                &Toplevel::clientStartUserMovedResized,
                this,
                &pointer_redirect::updateOnStartMoveResize);
        connect(c, &Toplevel::clientFinishUserMovedResized, this, &pointer_redirect::update);
    };
    const auto clients = workspace()->allClientList();
    std::for_each(clients.begin(), clients.end(), setupMoveResizeConnection);
    connect(workspace(), &Workspace::clientAdded, this, setupMoveResizeConnection);
    connect(waylandServer(), &WaylandServer::window_added, this, setupMoveResizeConnection);

    // warp the cursor to center of screen
    warp(screens()->geometry().center());
    updateAfterScreenChange();

    auto wayland_cursor = dynamic_cast<wayland::cursor*>(input::get_cursor());
    assert(wayland_cursor);
    connect(this,
            &pointer_redirect::decorationChanged,
            wayland_cursor->cursor_image.get(),
            &wayland::cursor_image::updateDecoration);
}

void pointer_redirect::updateOnStartMoveResize()
{
    breakPointerConstraints(focus() ? focus()->surface() : nullptr);
    disconnectPointerConstraintsConnection();
    setFocus(nullptr);
    waylandServer()->seat()->pointers().set_focused_surface(nullptr);
}

void pointer_redirect::updateToReset()
{
    if (internalWindow()) {
        disconnect(m_internalWindowConnection);
        m_internalWindowConnection = QMetaObject::Connection();
        QEvent event(QEvent::Leave);
        QCoreApplication::sendEvent(internalWindow(), &event);
        setInternalWindow(nullptr);
    }
    if (decoration()) {
        QHoverEvent event(QEvent::HoverLeave, QPointF(), QPointF());
        QCoreApplication::instance()->sendEvent(decoration()->decoration(), &event);
        setDecoration(nullptr);
    }
    if (auto focus_window = focus()) {
        if (focus_window->control) {
            win::leave_event(focus_window);
        }
        disconnect(m_focusGeometryConnection);
        m_focusGeometryConnection = QMetaObject::Connection();
        breakPointerConstraints(focus_window->surface());
        disconnectPointerConstraintsConnection();
        setFocus(nullptr);
    }
    waylandServer()->seat()->pointers().set_focused_surface(nullptr);
}

void pointer_redirect::processMotion(const QPointF& pos, uint32_t time, input::pointer* device)
{
    // Events for motion_absolute_event have positioning relative to screen size.
    auto const ssize = screens()->size();
    auto const rel_pos = QPointF(pos.x() / ssize.width(), pos.y() / ssize.height());

    auto event = motion_absolute_event{rel_pos, {device, time}};
    process_motion_absolute(event);
}

class PositionUpdateBlocker
{
public:
    PositionUpdateBlocker(pointer_redirect* pointer)
        : m_pointer(pointer)
    {
        s_counter++;
    }
    ~PositionUpdateBlocker()
    {
        s_counter--;
        if (s_counter == 0) {
            if (!s_scheduledPositions.isEmpty()) {
                auto const sched = s_scheduledPositions.takeFirst();
                if (sched.abs) {
                    m_pointer->process_motion_absolute({sched.pos, {nullptr, sched.time}});
                } else {
                    m_pointer->process_motion(
                        {sched.delta, sched.unaccel_delta, {nullptr, sched.time}});
                }
            }
        }
    }

    static bool isPositionBlocked()
    {
        return s_counter > 0;
    }

    static void schedule(QPointF const& pos, uint32_t time)
    {
        s_scheduledPositions.append({pos, {}, {}, time, true});
    }
    static void schedule(QPointF const& delta, QPointF const& unaccel_delta, uint32_t time)
    {
        s_scheduledPositions.append({{}, delta, unaccel_delta, time, false});
    }

private:
    static int s_counter;
    struct ScheduledPosition {
        QPointF pos;
        QPointF delta;
        QPointF unaccel_delta;
        uint32_t time;
        bool abs;
    };
    static QVector<ScheduledPosition> s_scheduledPositions;

    pointer_redirect* m_pointer;
};

int PositionUpdateBlocker::s_counter = 0;
QVector<PositionUpdateBlocker::ScheduledPosition> PositionUpdateBlocker::s_scheduledPositions;

void pointer_redirect::process_motion(motion_event const& event)
{
    if (!inited()) {
        return;
    }

    if (PositionUpdateBlocker::isPositionBlocked()) {
        PositionUpdateBlocker::schedule(event.delta, event.unaccel_delta, event.base.time_msec);
        return;
    }

    PositionUpdateBlocker blocker(this);

    auto const pos = this->pos() + QPointF(event.delta.x(), event.delta.y());
    updatePosition(pos);
    update();

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::motion, std::placeholders::_1, event));
    kwinApp()->input->redirect->processFilters(
        std::bind(&input::event_filter::motion, std::placeholders::_1, event));
}

void pointer_redirect::process_motion_absolute(motion_absolute_event const& event)
{
    if (!inited()) {
        return;
    }

    if (PositionUpdateBlocker::isPositionBlocked()) {
        PositionUpdateBlocker::schedule(event.pos, event.base.time_msec);
        return;
    }

    auto const ssize = screens()->size();
    auto const pos = QPointF(ssize.width() * event.pos.x(), ssize.height() * event.pos.y());

    PositionUpdateBlocker blocker(this);
    updatePosition(pos);
    update();

    auto motion_ev = motion_event({{}, {}, event.base});

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::motion, std::placeholders::_1, motion_ev));
    kwinApp()->input->redirect->processFilters(
        std::bind(&input::event_filter::motion, std::placeholders::_1, motion_ev));
}

void pointer_redirect::process_button(button_event const& event)
{
    if (event.state == button_state::pressed) {
        // Check focus before processing spies/filters.
        update();
    }

    update_button(event);

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::button, std::placeholders::_1, event));

    if (!inited()) {
        return;
    }

    kwinApp()->input->redirect->processFilters(
        std::bind(&input::event_filter::button, std::placeholders::_1, event));

    if (event.state == button_state::released) {
        // Check focus after processing spies/filters.
        update();
    }
}

void pointer_redirect::process_axis(axis_event const& event)
{
    update();

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::axis, std::placeholders::_1, event));

    if (!inited()) {
        return;
    }
    kwinApp()->input->redirect->processFilters(
        std::bind(&event_filter::axis, std::placeholders::_1, event));
}

void pointer_redirect::process_swipe_begin(swipe_begin_event const& event)
{
    if (!inited()) {
        return;
    }

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::swipe_begin, std::placeholders::_1, event));
    kwinApp()->input->redirect->processFilters(
        std::bind(&event_filter::swipe_begin, std::placeholders::_1, event));
}

void pointer_redirect::process_swipe_update(swipe_update_event const& event)
{
    if (!inited()) {
        return;
    }
    update();

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::swipe_update, std::placeholders::_1, event));
    kwinApp()->input->redirect->processFilters(
        std::bind(&event_filter::swipe_update, std::placeholders::_1, event));
}

void pointer_redirect::process_swipe_end(swipe_end_event const& event)
{
    if (!inited()) {
        return;
    }
    update();

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::swipe_end, std::placeholders::_1, event));
    kwinApp()->input->redirect->processFilters(
        std::bind(&event_filter::swipe_end, std::placeholders::_1, event));
}

void pointer_redirect::process_pinch_begin(pinch_begin_event const& event)
{
    if (!inited()) {
        return;
    }
    update();

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::pinch_begin, std::placeholders::_1, event));
    kwinApp()->input->redirect->processFilters(
        std::bind(&event_filter::pinch_begin, std::placeholders::_1, event));
}

void pointer_redirect::process_pinch_update(pinch_update_event const& event)
{
    if (!inited()) {
        return;
    }
    update();

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::pinch_update, std::placeholders::_1, event));
    kwinApp()->input->redirect->processFilters(
        std::bind(&event_filter::pinch_update, std::placeholders::_1, event));
}

void pointer_redirect::process_pinch_end(pinch_end_event const& event)
{
    if (!inited()) {
        return;
    }
    update();

    kwinApp()->input->redirect->processSpies(
        std::bind(&event_spy::pinch_end, std::placeholders::_1, event));
    kwinApp()->input->redirect->processFilters(
        std::bind(&event_filter::pinch_end, std::placeholders::_1, event));
}

bool pointer_redirect::areButtonsPressed() const
{
    for (auto state : m_buttons) {
        if (state == redirect::PointerButtonPressed) {
            return true;
        }
    }
    return false;
}

bool pointer_redirect::focusUpdatesBlocked()
{
    if (!inited()) {
        return true;
    }
    if (waylandServer()->seat()->drags().is_pointer_drag()) {
        // ignore during drag and drop
        return true;
    }
    if (waylandServer()->seat()->touches().is_in_progress()) {
        // ignore during touch operations
        return true;
    }
    if (kwinApp()->input->redirect->isSelectingWindow()) {
        return true;
    }
    if (areButtonsPressed()) {
        return true;
    }
    return false;
}

void pointer_redirect::cleanupInternalWindow(QWindow* old, QWindow* now)
{
    disconnect(m_internalWindowConnection);
    m_internalWindowConnection = QMetaObject::Connection();

    if (old) {
        // leave internal window
        QEvent leaveEvent(QEvent::Leave);
        QCoreApplication::sendEvent(old, &leaveEvent);
    }

    if (now) {
        m_internalWindowConnection
            = connect(internalWindow(), &QWindow::visibleChanged, this, [this](bool visible) {
                  if (!visible) {
                      update();
                  }
              });
    }
}

void pointer_redirect::cleanupDecoration(Decoration::DecoratedClientImpl* old,
                                         Decoration::DecoratedClientImpl* now)
{
    disconnect(m_decorationGeometryConnection);
    m_decorationGeometryConnection = QMetaObject::Connection();
    workspace()->updateFocusMousePosition(position().toPoint());

    if (old) {
        // send leave event to old decoration
        QHoverEvent event(QEvent::HoverLeave, QPointF(), QPointF());
        QCoreApplication::instance()->sendEvent(old->decoration(), &event);
    }
    if (!now) {
        // left decoration
        return;
    }

    waylandServer()->seat()->pointers().set_focused_surface(nullptr);

    auto pos = m_pos - now->client()->pos();
    QHoverEvent event(QEvent::HoverEnter, pos, pos);
    QCoreApplication::instance()->sendEvent(now->decoration(), &event);
    win::process_decoration_move(now->client(), pos.toPoint(), m_pos.toPoint());

    auto window = decoration()->client();

    m_decorationGeometryConnection
        = connect(window, &Toplevel::frame_geometry_changed, this, [this, window] {
              if (window->control && (win::is_move(window) || win::is_resize(window))) {
                  // Don't update while doing an interactive move or resize.
                  return;
              }
              // ensure maximize button gets the leave event when maximizing/restore a window, see
              // BUG 385140
              const auto oldDeco = decoration();
              update();
              if (oldDeco && oldDeco == decoration() && !win::is_move(decoration()->client())
                  && !win::is_resize(decoration()->client()) && !areButtonsPressed()) {
                  // position of window did not change, we need to send HoverMotion manually
                  const QPointF p = m_pos - decoration()->client()->pos();
                  QHoverEvent event(QEvent::HoverMove, p, p);
                  QCoreApplication::instance()->sendEvent(decoration()->decoration(), &event);
              }
          });
}

void pointer_redirect::focusUpdate(Toplevel* focusOld, Toplevel* focusNow)
{
    if (focusOld) {
        // Need to check on control because of Xwayland unmanaged windows.
        if (auto lead = win::lead_of_annexed_transient(focusOld); lead && lead->control) {
            win::leave_event(lead);
        }
        breakPointerConstraints(focusOld->surface());
        disconnectPointerConstraintsConnection();
    }
    disconnect(m_focusGeometryConnection);
    m_focusGeometryConnection = QMetaObject::Connection();

    if (focusNow) {
        if (auto lead = win::lead_of_annexed_transient(focusNow)) {
            win::enter_event(lead, m_pos.toPoint());
        }
        workspace()->updateFocusMousePosition(m_pos.toPoint());
    }

    if (internalWindow()) {
        // enter internal window
        const auto pos = at()->pos();
        QEnterEvent enterEvent(pos, pos, m_pos);
        QCoreApplication::sendEvent(internalWindow(), &enterEvent);
    }

    auto seat = waylandServer()->seat();
    if (!focusNow || !focusNow->surface() || decoration()) {
        // Clean up focused pointer surface if there's no client to take focus,
        // or the pointer is on a client without surface or on a decoration.
        warpXcbOnSurfaceLeft(nullptr);
        seat->pointers().set_focused_surface(nullptr);
        return;
    }

    // TODO: add convenient API to update global pos together with updating focused surface
    warpXcbOnSurfaceLeft(focusNow->surface());

    // TODO: why? in order to reset the cursor icon?
    s_cursorUpdateBlocking = true;
    seat->pointers().set_focused_surface(nullptr);
    s_cursorUpdateBlocking = false;

    seat->pointers().set_position(m_pos.toPoint());
    seat->pointers().set_focused_surface(focusNow->surface(), focusNow->input_transform());

    m_focusGeometryConnection = connect(focusNow, &Toplevel::frame_geometry_changed, this, [this] {
        if (!focus()) {
            // Might happen for Xwayland clients.
            return;
        }

        // TODO: can we check on the client instead?
        if (workspace()->moveResizeClient()) {
            // don't update while moving
            return;
        }
        auto seat = waylandServer()->seat();
        if (focus()->surface() != seat->pointers().get_focus().surface) {
            return;
        }
        seat->pointers().set_focused_surface_transformation(focus()->input_transform());
    });

    m_constraintsConnection = connect(focusNow->surface(),
                                      &Wrapland::Server::Surface::pointerConstraintsChanged,
                                      this,
                                      &pointer_redirect::updatePointerConstraints);
    m_constraintsActivatedConnection = connect(workspace(),
                                               &Workspace::clientActivated,
                                               this,
                                               &pointer_redirect::updatePointerConstraints);
    updatePointerConstraints();
}

void pointer_redirect::breakPointerConstraints(Wrapland::Server::Surface* surface)
{
    // cancel pointer constraints
    if (surface) {
        auto c = surface->confinedPointer();
        if (c && c->isConfined()) {
            c->setConfined(false);
        }
        auto l = surface->lockedPointer();
        if (l && l->isLocked()) {
            l->setLocked(false);
        }
    }
    disconnectConfinedPointerRegionConnection();
    m_confined = false;
    m_locked = false;
}

void pointer_redirect::disconnectConfinedPointerRegionConnection()
{
    disconnect(m_confinedPointerRegionConnection);
    m_confinedPointerRegionConnection = QMetaObject::Connection();
}

void pointer_redirect::disconnectLockedPointerDestroyedConnection()
{
    disconnect(m_lockedPointerDestroyedConnection);
    m_lockedPointerDestroyedConnection = QMetaObject::Connection();
}

void pointer_redirect::disconnectPointerConstraintsConnection()
{
    disconnect(m_constraintsConnection);
    m_constraintsConnection = QMetaObject::Connection();

    disconnect(m_constraintsActivatedConnection);
    m_constraintsActivatedConnection = QMetaObject::Connection();
}

template<typename T>
static QRegion getConstraintRegion(Toplevel* t, T* constraint)
{
    if (!t->surface()) {
        return QRegion();
    }

    QRegion constraint_region;

    if (t->surface()->state().input_is_infinite) {
        auto const client_size = win::frame_relative_client_rect(t).size();
        constraint_region = QRegion(0, 0, client_size.width(), client_size.height());
    } else {
        constraint_region = t->surface()->state().input;
    }

    if (auto const& reg = constraint->region(); !reg.isEmpty()) {
        constraint_region = constraint_region.intersected(reg);
    }

    return constraint_region.translated(win::frame_to_client_pos(t, t->pos()));
}

void pointer_redirect::setEnableConstraints(bool set)
{
    if (m_enableConstraints == set) {
        return;
    }
    m_enableConstraints = set;
    updatePointerConstraints();
}

void pointer_redirect::updatePointerConstraints()
{
    if (!focus()) {
        return;
    }
    const auto s = focus()->surface();
    if (!s) {
        return;
    }
    if (s != waylandServer()->seat()->pointers().get_focus().surface) {
        return;
    }
    if (!supportsWarping()) {
        return;
    }
    const bool canConstrain = m_enableConstraints && focus() == workspace()->activeClient();
    const auto cf = s->confinedPointer();
    if (cf) {
        if (cf->isConfined()) {
            if (!canConstrain) {
                cf->setConfined(false);
                m_confined = false;
                disconnectConfinedPointerRegionConnection();
            }
            return;
        }
        const QRegion r = getConstraintRegion(focus(), cf.data());
        if (canConstrain && r.contains(m_pos.toPoint())) {
            cf->setConfined(true);
            m_confined = true;
            m_confinedPointerRegionConnection = connect(
                cf.data(), &Wrapland::Server::ConfinedPointerV1::regionChanged, this, [this] {
                    if (!focus()) {
                        return;
                    }
                    const auto s = focus()->surface();
                    if (!s) {
                        return;
                    }
                    const auto cf = s->confinedPointer();
                    if (!getConstraintRegion(focus(), cf.data()).contains(m_pos.toPoint())) {
                        // pointer no longer in confined region, break the confinement
                        cf->setConfined(false);
                        m_confined = false;
                    } else {
                        if (!cf->isConfined()) {
                            cf->setConfined(true);
                            m_confined = true;
                        }
                    }
                });
            return;
        }
    } else {
        m_confined = false;
        disconnectConfinedPointerRegionConnection();
    }
    const auto lock = s->lockedPointer();
    if (lock) {
        if (lock->isLocked()) {
            if (!canConstrain) {
                const auto hint = lock->cursorPositionHint();
                lock->setLocked(false);
                m_locked = false;
                disconnectLockedPointerDestroyedConnection();
                if (!(hint.x() < 0 || hint.y() < 0) && focus()) {
                    // TODO(romangg): different client offset for Xwayland clients?
                    processMotion(win::frame_to_client_pos(focus(), focus()->pos()) + hint,
                                  waylandServer()->seat()->timestamp());
                }
            }
            return;
        }
        const QRegion r = getConstraintRegion(focus(), lock.data());
        if (canConstrain && r.contains(m_pos.toPoint())) {
            lock->setLocked(true);
            m_locked = true;

            // The client might cancel pointer locking from its side by unbinding the
            // LockedPointerV1. In this case the cached cursor position hint must be fetched before
            // the resource goes away
            m_lockedPointerDestroyedConnection
                = connect(lock.data(),
                          &Wrapland::Server::LockedPointerV1::resourceDestroyed,
                          this,
                          [this, lock]() {
                              const auto hint = lock->cursorPositionHint();
                              if (hint.x() < 0 || hint.y() < 0 || !focus()) {
                                  return;
                              }
                              // TODO(romangg): different client offset for Xwayland clients?
                              auto globalHint
                                  = win::frame_to_client_pos(focus(), focus()->pos()) + hint;
                              processMotion(globalHint, waylandServer()->seat()->timestamp());
                          });
            // TODO: connect to region change - is it needed at all? If the pointer is locked it's
            // always in the region
        }
    } else {
        m_locked = false;
        disconnectLockedPointerDestroyedConnection();
    }
}

void pointer_redirect::warpXcbOnSurfaceLeft(Wrapland::Server::Surface* newSurface)
{
    auto xc = waylandServer()->xWaylandConnection();
    if (!xc) {
        // No XWayland, no point in warping the x cursor
        return;
    }
    const auto c = kwinApp()->x11Connection();
    if (!c) {
        return;
    }
    static bool s_hasXWayland119 = xcb_get_setup(c)->release_number >= 11900000;
    if (s_hasXWayland119) {
        return;
    }
    if (newSurface && newSurface->client() == xc) {
        // new window is an X window
        return;
    }
    auto s = waylandServer()->seat()->pointers().get_focus().surface;
    if (!s || s->client() != xc) {
        // pointer was not on an X window
        return;
    }
    // warp pointer to 0/0 to trigger leave events on previously focused X window
    xcb_warp_pointer(c, XCB_WINDOW_NONE, kwinApp()->x11RootWindow(), 0, 0, 0, 0, 0, 0),
        xcb_flush(c);
}

QPointF pointer_redirect::applyPointerConfinement(const QPointF& pos) const
{
    if (!focus()) {
        return pos;
    }
    auto s = focus()->surface();
    if (!s) {
        return pos;
    }
    auto cf = s->confinedPointer();
    if (!cf) {
        return pos;
    }
    if (!cf->isConfined()) {
        return pos;
    }

    const QRegion confinementRegion = getConstraintRegion(focus(), cf.data());
    if (confinementRegion.contains(pos.toPoint())) {
        return pos;
    }
    QPointF p = pos;
    // allow either x or y to pass
    p = QPointF(m_pos.x(), pos.y());
    if (confinementRegion.contains(p.toPoint())) {
        return p;
    }
    p = QPointF(pos.x(), m_pos.y());
    if (confinementRegion.contains(p.toPoint())) {
        return p;
    }

    return m_pos;
}

void pointer_redirect::updatePosition(const QPointF& pos)
{
    if (m_locked) {
        // locked pointer should not move
        return;
    }
    // verify that at least one screen contains the pointer position
    QPointF p = pos;
    if (!screenContainsPos(p)) {
        const QRectF unitedScreensGeometry = screens()->geometry();
        p = confineToBoundingBox(p, unitedScreensGeometry);
        if (!screenContainsPos(p)) {
            const QRectF currentScreenGeometry
                = screens()->geometry(screens()->number(m_pos.toPoint()));
            p = confineToBoundingBox(p, currentScreenGeometry);
        }
    }
    p = applyPointerConfinement(p);
    if (p == m_pos) {
        // didn't change due to confinement
        return;
    }
    // verify screen confinement
    if (!screenContainsPos(p)) {
        return;
    }
    m_pos = p;
    emit kwinApp()->input->redirect->globalPointerChanged(m_pos);
}

void pointer_redirect::update_button(button_event const& event)
{
    auto internal_state = event.state == button_state::pressed
        ? input::redirect::PointerButtonPressed
        : input::redirect::PointerButtonReleased;

    m_buttons[event.key] = internal_state;

    // update Qt buttons
    m_qtButtons = Qt::NoButton;
    for (auto it = m_buttons.constBegin(); it != m_buttons.constEnd(); ++it) {
        if (it.value() == redirect::PointerButtonReleased) {
            continue;
        }
        m_qtButtons |= button_to_qt_mouse_button(it.key());
    }

    Q_EMIT kwinApp()->input->redirect->pointerButtonStateChanged(event.key, internal_state);
}

void pointer_redirect::warp(const QPointF& pos)
{
    if (supportsWarping()) {
        kwinApp()->platform->warpPointer(pos);
        processMotion(pos, waylandServer()->seat()->timestamp());
    }
}

bool pointer_redirect::supportsWarping() const
{
    if (!inited()) {
        return false;
    }
    if (m_supportsWarping) {
        return true;
    }
    if (kwinApp()->platform->supportsPointerWarping()) {
        return true;
    }
    return false;
}

void pointer_redirect::updateAfterScreenChange()
{
    if (!inited()) {
        return;
    }
    if (screenContainsPos(m_pos)) {
        // pointer still on a screen
        return;
    }
    // pointer no longer on a screen, reposition to closes screen
    const QPointF pos = screens()->geometry(screens()->number(m_pos.toPoint())).center();
    // TODO: better way to get timestamps
    processMotion(pos, waylandServer()->seat()->timestamp());
}

QPointF pointer_redirect::position() const
{
    return m_pos.toPoint();
}

void pointer_redirect::setEffectsOverrideCursor(Qt::CursorShape shape)
{
    if (!inited()) {
        return;
    }
    // current pointer focus window should get a leave event
    update();
    auto wayland_cursor = static_cast<wayland::cursor*>(input::get_cursor());
    wayland_cursor->cursor_image->setEffectsOverrideCursor(shape);
}

void pointer_redirect::removeEffectsOverrideCursor()
{
    if (!inited()) {
        return;
    }
    // cursor position might have changed while there was an effect in place
    update();
    auto wayland_cursor = static_cast<wayland::cursor*>(input::get_cursor());
    wayland_cursor->cursor_image->removeEffectsOverrideCursor();
}

void pointer_redirect::setWindowSelectionCursor(const QByteArray& shape)
{
    if (!inited()) {
        return;
    }
    // send leave to current pointer focus window
    updateToReset();
    auto wayland_cursor = static_cast<wayland::cursor*>(input::get_cursor());
    wayland_cursor->cursor_image->setWindowSelectionCursor(shape);
}

void pointer_redirect::removeWindowSelectionCursor()
{
    if (!inited()) {
        return;
    }
    update();
    auto wayland_cursor = static_cast<wayland::cursor*>(input::get_cursor());
    wayland_cursor->cursor_image->removeWindowSelectionCursor();
}

}
