/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "device_redirect.h"
#include "motion_scheduler.h"

#include "base/wayland/server.h"
#include "input/device_redirect.h"
#include "input/event_filter.h"
#include "input/event_spy.h"
#include "input/qt_event.h"
#include "utils/blocker.h"

#include <KScreenLocker/KsldApp>
#include <QObject>
#include <Wrapland/Server/drag_pool.h>
#include <Wrapland/Server/pointer_constraints_v1.h>
#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/touch_pool.h>

namespace KWin::input::wayland
{

template<typename Window, typename T>
static QRegion getConstraintRegion(Window* t, T* constraint)
{
    if (!t->surface) {
        return QRegion();
    }

    QRegion constraint_region;

    if (t->surface->state().input_is_infinite) {
        auto const client_size = win::frame_relative_client_rect(t).size();
        constraint_region = QRegion(0, 0, client_size.width(), client_size.height());
    } else {
        constraint_region = t->surface->state().input;
    }

    if (auto const& reg = constraint->region(); !reg.isEmpty()) {
        constraint_region = constraint_region.intersected(reg);
    }

    return constraint_region.translated(win::frame_to_client_pos(t, t->geo.pos()));
}

template<typename Redirect>
class pointer_redirect
{
public:
    using space_t = typename Redirect::platform_t::base_t::space_t;
    using window_t = typename space_t::window_t;

    explicit pointer_redirect(Redirect* redirect)
        : qobject{std::make_unique<QObject>()}
        , redirect{redirect}
        , motions{*this}
    {
    }

    void init()
    {
        device_redirect_init(this);

        QObject::connect(&kwinApp()->get_base(),
                         &base::platform::topology_changed,
                         qobject.get(),
                         [this] { updateAfterScreenChange(); });
        if (waylandServer()->has_screen_locker_integration()) {
            QObject::connect(ScreenLocker::KSldApp::self(),
                             &ScreenLocker::KSldApp::lockStateChanged,
                             qobject.get(),
                             [this] {
                                 waylandServer()->seat()->pointers().cancel_pinch_gesture();
                                 waylandServer()->seat()->pointers().cancel_swipe_gesture();
                                 device_redirect_update(this);
                             });
        }

        QObject::connect(
            waylandServer()->seat(), &Wrapland::Server::Seat::dragEnded, qobject.get(), [this] {
                // need to force a focused pointer change
                waylandServer()->seat()->pointers().set_focused_surface(nullptr);
                device_redirect_unset_focus(this);
                device_redirect_update(this);
            });

        // connect the move resize of all window
        auto setupMoveResizeConnection = [this](auto c) {
            if (!c->control) {
                return;
            }
            QObject::connect(c->qobject.get(),
                             &win::window_qobject::clientStartUserMovedResized,
                             qobject.get(),
                             [this] { update_on_start_move_resize(); });
            QObject::connect(c->qobject.get(),
                             &win::window_qobject::clientFinishUserMovedResized,
                             qobject.get(),
                             [this] { device_redirect_update(this); });
        };
        auto setup_move_resize_notify_on_signal = [this, setupMoveResizeConnection](auto win_id) {
            auto c = redirect->space.windows_map.at(win_id);
            setupMoveResizeConnection(c);
        };

        auto const clients = redirect->space.windows;
        std::for_each(clients.begin(), clients.end(), setupMoveResizeConnection);
        QObject::connect(redirect->space.qobject.get(),
                         &win::space_qobject::clientAdded,
                         qobject.get(),
                         setup_move_resize_notify_on_signal);
        QObject::connect(redirect->space.qobject.get(),
                         &win::space_qobject::wayland_window_added,
                         qobject.get(),
                         setup_move_resize_notify_on_signal);

        // warp the cursor to center of screen
        warp(QRect({}, kwinApp()->get_base().topology.size).center());
        updateAfterScreenChange();
    }

    void updateAfterScreenChange()
    {
        if (screenContainsPos(m_pos)) {
            // pointer still on a screen
            return;
        }

        // pointer no longer on a screen, reposition to closes screen
        auto const& outputs = redirect->platform.base.outputs;
        if (outputs.empty()) {
            return;
        }

        auto output = base::get_nearest_output(outputs, m_pos.toPoint());
        QPointF const pos = output->geometry().center();

        // TODO: better way to get timestamps
        processMotion(pos, waylandServer()->seat()->timestamp());
    }

    void warp(QPointF const& pos)
    {
        processMotion(pos, waylandServer()->seat()->timestamp());
    }

    QPointF pos() const
    {
        return m_pos;
    }

    Qt::MouseButtons buttons() const
    {
        return qt_buttons;
    }

    bool areButtonsPressed() const
    {
        for (auto state : m_buttons) {
            if (state == button_state::pressed) {
                return true;
            }
        }
        return false;
    }

    void setEffectsOverrideCursor(Qt::CursorShape shape)
    {
        // current pointer focus window should get a leave event
        device_redirect_update(this);
        auto wayland_cursor = redirect->cursor.get();
        wayland_cursor->cursor_image->setEffectsOverrideCursor(shape);
    }

    void removeEffectsOverrideCursor()
    {
        // cursor position might have changed while there was an effect in place
        device_redirect_update(this);
        redirect->cursor->cursor_image->removeEffectsOverrideCursor();
    }

    void setWindowSelectionCursor(QByteArray const& shape)
    {
        // send leave to current pointer focus window
        update_to_reset();
        redirect->cursor->cursor_image->setWindowSelectionCursor(shape);
    }

    void removeWindowSelectionCursor()
    {
        device_redirect_update(this);
        redirect->cursor->cursor_image->removeWindowSelectionCursor();
    }

    bool can_constrain() const
    {
        return constraints.enabled && focus.window == redirect->space.stacking.active;
    }

    template<typename Win>
    bool update_confinement(Win& win)
    {
        auto const surface = win.surface;
        assert(surface);

        auto const cf = surface->confinedPointer();
        if (!cf) {
            constraints.confined = false;
            disconnect_confined_pointer_region_connection();
            return false;
        }

        if (cf->isConfined()) {
            if (!can_constrain()) {
                cf->setConfined(false);
                constraints.confined = false;
                disconnect_confined_pointer_region_connection();
            }
            return false;
        }

        if (!can_constrain() || !getConstraintRegion(&win, cf.data()).contains(m_pos.toPoint())) {
            return false;
        }

        cf->setConfined(true);
        constraints.confined = true;

        notifiers.confined_pointer_region = QObject::connect(
            cf.data(),
            &Wrapland::Server::ConfinedPointerV1::regionChanged,
            qobject.get(),
            [&win, this] {
                assert(win.surface);
                auto const cf = win.surface->confinedPointer();
                if (getConstraintRegion(&win, cf.data()).contains(m_pos.toPoint())) {
                    if (!cf->isConfined()) {
                        cf->setConfined(true);
                        constraints.confined = true;
                    }
                    return;
                }

                // Pointer no longer in confined region, break the confinement.
                cf->setConfined(false);
                constraints.confined = false;
            });
        return true;
    }

    template<typename Win>
    void update_lock(Win& win)
    {
        auto const surface = win.surface;
        assert(surface);

        auto const lock = surface->lockedPointer();
        if (!lock) {
            constraints.locked = false;
            disconnect_locked_pointer_destroyed_connection();
            return;
        }

        if (lock->isLocked()) {
            if (!can_constrain()) {
                auto const hint = lock->cursorPositionHint();
                lock->setLocked(false);
                constraints.locked = false;
                disconnect_locked_pointer_destroyed_connection();
                if (!(hint.x() < 0 || hint.y() < 0)) {
                    // TODO(romangg): different client offset for Xwayland clients?
                    processMotion(win::frame_to_client_pos(&win, win.geo.pos()) + hint,
                                  waylandServer()->seat()->timestamp());
                }
            }
            return;
        }

        if (can_constrain() && getConstraintRegion(&win, lock.data()).contains(m_pos.toPoint())) {
            lock->setLocked(true);
            constraints.locked = true;

            // The client might cancel pointer locking from its side by unbinding the
            // LockedPointerV1. In this case the cached cursor position hint must be fetched
            // before the resource goes away
            notifiers.locked_pointer_destroyed = QObject::connect(
                lock.data(),
                &Wrapland::Server::LockedPointerV1::resourceDestroyed,
                qobject.get(),
                [&win, lock, this]() {
                    assert(win.surface);
                    auto const hint = lock->cursorPositionHint();
                    if (hint.x() < 0 || hint.y() < 0) {
                        return;
                    }
                    // TODO(romangg): different client offset for Xwayland clients?
                    auto globalHint = win::frame_to_client_pos(&win, win.geo.pos()) + hint;
                    processMotion(globalHint, waylandServer()->seat()->timestamp());
                });
            // TODO: connect to region change - is it needed at all? If the pointer is locked
            // it's always in the region
        }
    }

    void updatePointerConstraints()
    {
        if (!focus.window) {
            return;
        }

        const auto s = focus.window->surface;
        if (!s) {
            return;
        }

        auto seat = waylandServer()->seat();
        if (!seat->hasPointer()) {
            return;
        }

        if (s != seat->pointers().get_focus().surface) {
            return;
        }

        if (update_confinement(*focus.window)) {
            // Pointer is confined. Don't lock.
            // TODO(romangg): Should we disable the lock?
            return;
        }

        update_lock(*focus.window);
    }

    void setEnableConstraints(bool set)
    {
        if (constraints.enabled == set) {
            return;
        }
        constraints.enabled = set;
        updatePointerConstraints();
    }

    bool isConstrained() const
    {
        return constraints.confined || constraints.locked;
    }

    bool focusUpdatesBlocked()
    {
        if (waylandServer()->seat()->drags().is_pointer_drag()) {
            // ignore during drag and drop
            return true;
        }
        if (waylandServer()->seat()->hasTouch()
            && waylandServer()->seat()->touches().is_in_progress()) {
            // ignore during touch operations
            return true;
        }
        if (redirect->isSelectingWindow()) {
            return true;
        }
        if (areButtonsPressed()) {
            return true;
        }
        return false;
    }

    void process_motion(motion_event const& event)
    {
        if (motions.is_locked()) {
            motions.schedule(event.delta, event.unaccel_delta, event.base.time_msec);
            return;
        }

        blocker block(&motions);

        auto const pos = this->pos() + QPointF(event.delta.x(), event.delta.y());
        update_position(pos);
        device_redirect_update(this);

        process_spies(redirect->m_spies,
                      std::bind(&event_spy<Redirect>::motion, std::placeholders::_1, event));
        process_filters(redirect->m_filters,
                        std::bind(&event_filter<Redirect>::motion, std::placeholders::_1, event));

        process_frame();
    }

    void process_motion_absolute(motion_absolute_event const& event)
    {
        if (motions.is_locked()) {
            motions.schedule(event.pos, event.base.time_msec);
            return;
        }

        auto const& space_size = redirect->platform.base.topology.size;
        auto const pos
            = QPointF(space_size.width() * event.pos.x(), space_size.height() * event.pos.y());

        blocker block(&motions);
        update_position(pos);
        device_redirect_update(this);

        auto motion_ev = motion_event({{}, {}, event.base});

        process_spies(redirect->m_spies,
                      std::bind(&event_spy<Redirect>::motion, std::placeholders::_1, motion_ev));
        process_filters(
            redirect->m_filters,
            std::bind(&event_filter<Redirect>::motion, std::placeholders::_1, motion_ev));

        process_frame();
    }

    void processMotion(QPointF const& pos, uint32_t time, KWin::input::pointer* device = nullptr)
    {
        // Events for motion_absolute_event have positioning relative to screen size.
        auto const& space_size = kwinApp()->get_base().topology.size;
        auto const rel_pos = QPointF(pos.x() / space_size.width(), pos.y() / space_size.height());

        auto event = motion_absolute_event{rel_pos, {device, time}};
        process_motion_absolute(event);
    }

    void process_button(button_event const& event)
    {
        if (event.state == button_state::pressed) {
            // Check focus before processing spies/filters.
            device_redirect_update(this);
        }

        update_button(event);
        pointer_redirect_process_button_spies(*this, event);
        process_filters(redirect->m_filters,
                        std::bind(&event_filter<Redirect>::button, std::placeholders::_1, event));

        if (event.state == button_state::released) {
            // Check focus after processing spies/filters.
            device_redirect_update(this);
        }

        process_frame();
    }

    void process_axis(axis_event const& event)
    {
        device_redirect_update(this);

        process_spies(redirect->m_spies,
                      std::bind(&event_spy<Redirect>::axis, std::placeholders::_1, event));
        process_filters(redirect->m_filters,
                        std::bind(&event_filter<Redirect>::axis, std::placeholders::_1, event));

        process_frame();
    }

    void process_swipe_begin(swipe_begin_event const& event)
    {
        process_spies(redirect->m_spies,
                      std::bind(&event_spy<Redirect>::swipe_begin, std::placeholders::_1, event));
        process_filters(
            redirect->m_filters,
            std::bind(&event_filter<Redirect>::swipe_begin, std::placeholders::_1, event));
    }

    void process_swipe_update(swipe_update_event const& event)
    {
        device_redirect_update(this);

        process_spies(redirect->m_spies,
                      std::bind(&event_spy<Redirect>::swipe_update, std::placeholders::_1, event));
        process_filters(
            redirect->m_filters,
            std::bind(&event_filter<Redirect>::swipe_update, std::placeholders::_1, event));
    }

    void process_swipe_end(swipe_end_event const& event)
    {
        device_redirect_update(this);

        process_spies(redirect->m_spies,
                      std::bind(&event_spy<Redirect>::swipe_end, std::placeholders::_1, event));
        process_filters(
            redirect->m_filters,
            std::bind(&event_filter<Redirect>::swipe_end, std::placeholders::_1, event));
    }

    void process_pinch_begin(pinch_begin_event const& event)
    {
        device_redirect_update(this);

        process_spies(redirect->m_spies,
                      std::bind(&event_spy<Redirect>::pinch_begin, std::placeholders::_1, event));
        process_filters(
            redirect->m_filters,
            std::bind(&event_filter<Redirect>::pinch_begin, std::placeholders::_1, event));
    }

    void process_pinch_update(pinch_update_event const& event)
    {
        device_redirect_update(this);

        process_spies(redirect->m_spies,
                      std::bind(&event_spy<Redirect>::pinch_update, std::placeholders::_1, event));
        process_filters(
            redirect->m_filters,
            std::bind(&event_filter<Redirect>::pinch_update, std::placeholders::_1, event));
    }

    void process_pinch_end(pinch_end_event const& event)
    {
        device_redirect_update(this);

        process_spies(redirect->m_spies,
                      std::bind(&event_spy<Redirect>::pinch_end, std::placeholders::_1, event));
        process_filters(
            redirect->m_filters,
            std::bind(&event_filter<Redirect>::pinch_end, std::placeholders::_1, event));
    }

    void process_frame()
    {
        waylandServer()->seat()->pointers().frame();
    }

    void cleanupInternalWindow(QWindow* old, QWindow* now)
    {
        QObject::disconnect(notifiers.internal_window);
        notifiers.internal_window = QMetaObject::Connection();

        if (old) {
            // leave internal window
            QEvent leaveEvent(QEvent::Leave);
            QCoreApplication::sendEvent(old, &leaveEvent);
        }

        if (now) {
            notifiers.internal_window = QObject::connect(focus.internal_window,
                                                         &QWindow::visibleChanged,
                                                         qobject.get(),
                                                         [this](bool visible) {
                                                             if (!visible) {
                                                                 device_redirect_update(this);
                                                             }
                                                         });
        }
    }

    void unset_deco()
    {
        assert(focus.deco);

        QObject::disconnect(notifiers.decoration_geometry);
        notifiers.decoration_geometry = QMetaObject::Connection();
        redirect->space.focusMousePos = position().toPoint();

        // send leave event to decoration
        QHoverEvent event(QEvent::HoverLeave, QPointF(), QPointF());
        QCoreApplication::instance()->sendEvent(focus.deco->decoration(), &event);

        focus.deco = nullptr;
        redirect->cursor->cursor_image->unset_deco();
    }

    void set_deco(win::deco::client_impl<typename space_t::window_t>& now)
    {
        assert(!focus.deco);

        waylandServer()->seat()->pointers().set_focused_surface(nullptr);

        auto pos = m_pos - now.client()->geo.pos();
        QHoverEvent event(QEvent::HoverEnter, pos, pos);
        QCoreApplication::instance()->sendEvent(now.decoration(), &event);
        win::process_decoration_move(now.client(), pos.toPoint(), m_pos.toPoint());

        auto window = now.client();

        notifiers.decoration_geometry = QObject::connect(
            window->qobject.get(),
            &win::window_qobject::frame_geometry_changed,
            qobject.get(),
            [this, window] {
                if (window->control && (win::is_move(window) || win::is_resize(window))) {
                    // Don't update while doing an interactive move or resize.
                    return;
                }
                // ensure maximize button gets the leave event when maximizing/restore a window, see
                // BUG 385140
                auto const old_deco = focus.deco;
                device_redirect_update(this);
                auto deco = focus.deco;
                if (old_deco && old_deco == deco && !win::is_move(deco->client())
                    && !win::is_resize(deco->client()) && !areButtonsPressed()) {
                    // position of window did not change, we need to send HoverMotion manually
                    QPointF const p = m_pos - deco->client()->geo.pos();
                    QHoverEvent event(QEvent::HoverMove, p, p);
                    QCoreApplication::instance()->sendEvent(deco->decoration(), &event);
                }
            });

        focus.deco = &now;
        redirect->cursor->cursor_image->set_deco(now);
    }

    void focusUpdate(typename space_t::window_t* focusOld, typename space_t::window_t* focusNow)
    {
        if (focusOld) {
            // Need to check on control because of Xwayland unmanaged windows.
            if (auto lead = win::lead_of_annexed_transient(focusOld); lead && lead->control) {
                win::leave_event(lead);
            }
            break_pointer_constraints(focusOld->surface);
            disconnect_pointer_constraints_connection();
        }
        QObject::disconnect(notifiers.focus_geometry);
        notifiers.focus_geometry = QMetaObject::Connection();

        if (focusNow) {
            if (auto lead = win::lead_of_annexed_transient(focusNow)) {
                win::enter_event(lead, m_pos.toPoint());
            }
            redirect->space.focusMousePos = m_pos.toPoint();
        }

        if (auto focus_internal = focus.internal_window) {
            // enter internal window
            auto const pos = at.window->geo.pos();
            QEnterEvent enterEvent(pos, pos, m_pos);
            QCoreApplication::sendEvent(focus_internal, &enterEvent);
        }

        auto seat = waylandServer()->seat();
        if (!focusNow || !focusNow->surface || focus.deco) {
            // Clean up focused pointer surface if there's no client to take focus,
            // or the pointer is on a client without surface or on a decoration.
            warp_xcb_on_surface_left(nullptr);
            seat->pointers().set_focused_surface(nullptr);
            return;
        }

        // TODO: add convenient API to update global pos together with updating focused surface
        warp_xcb_on_surface_left(focusNow->surface);

        // TODO: why? in order to reset the cursor icon?
        cursor_update_blocking = true;
        seat->pointers().set_focused_surface(nullptr);
        cursor_update_blocking = false;

        seat->pointers().set_position(m_pos.toPoint());
        seat->pointers().set_focused_surface(focusNow->surface,
                                             win::get_input_transform(*focusNow));

        notifiers.focus_geometry = QObject::connect(
            focusNow->qobject.get(),
            &win::window_qobject::frame_geometry_changed,
            qobject.get(),
            [this] {
                if (!focus.window) {
                    // Might happen for Xwayland clients.
                    return;
                }

                // TODO: can we check on the client instead?
                if (redirect->space.move_resize_window) {
                    // don't update while moving
                    return;
                }
                auto seat = waylandServer()->seat();
                if (focus.window->surface != seat->pointers().get_focus().surface) {
                    return;
                }
                seat->pointers().set_focused_surface_transformation(
                    win::get_input_transform(*focus.window));
            });

        notifiers.constraints
            = QObject::connect(focusNow->surface,
                               &Wrapland::Server::Surface::pointerConstraintsChanged,
                               qobject.get(),
                               [this] { updatePointerConstraints(); });
        notifiers.constraints_activated = QObject::connect(redirect->space.qobject.get(),
                                                           &win::space_qobject::clientActivated,
                                                           qobject.get(),
                                                           [this] { updatePointerConstraints(); });
        updatePointerConstraints();
    }

    QPointF position() const
    {
        return m_pos.toPoint();
    }

    std::unique_ptr<QObject> qobject;
    Redirect* redirect;

    device_redirect_at<window_t> at;
    device_redirect_focus<window_t> focus;

    bool cursor_update_blocking{false};

private:
    bool screenContainsPos(QPointF const& pos) const
    {
        for (auto output : redirect->platform.base.outputs) {
            if (output->geometry().contains(pos.toPoint())) {
                return true;
            }
        }
        return false;
    }

    void update_on_start_move_resize()
    {
        break_pointer_constraints(focus.window ? focus.window->surface : nullptr);
        disconnect_pointer_constraints_connection();
        device_redirect_unset_focus(this);
        waylandServer()->seat()->pointers().set_focused_surface(nullptr);
    }

    void update_to_reset()
    {
        if (auto focus_internal = focus.internal_window) {
            QObject::disconnect(notifiers.internal_window);
            notifiers.internal_window = QMetaObject::Connection();
            QEvent event(QEvent::Leave);
            QCoreApplication::sendEvent(focus_internal, &event);
            device_redirect_set_internal_window(this, nullptr);
        }
        if (focus.deco) {
            QHoverEvent event(QEvent::HoverLeave, QPointF(), QPointF());
            QCoreApplication::instance()->sendEvent(focus.deco->decoration(), &event);
            device_redirect_unset_deco(this);
        }
        if (auto focus_window = focus.window) {
            if (focus_window->control) {
                win::leave_event(focus_window);
            }
            QObject::disconnect(notifiers.focus_geometry);
            notifiers.focus_geometry = QMetaObject::Connection();
            break_pointer_constraints(focus_window->surface);
            disconnect_pointer_constraints_connection();
            device_redirect_unset_focus(this);
        }
        waylandServer()->seat()->pointers().set_focused_surface(nullptr);
    }

    void update_position(QPointF pos)
    {
        auto confineToBoundingBox = [](QPointF const& pos, QRectF const& boundingBox) {
            return QPointF(qBound(boundingBox.left(), pos.x(), boundingBox.right() - 1.0),
                           qBound(boundingBox.top(), pos.y(), boundingBox.bottom() - 1.0));
        };

        if (constraints.locked) {
            // locked pointer should not move
            return;
        }

        // verify that at least one screen contains the pointer position
        if (!screenContainsPos(pos)) {
            auto const unitedScreensGeometry = QRectF({}, kwinApp()->get_base().topology.size);
            pos = confineToBoundingBox(pos, unitedScreensGeometry);

            if (!screenContainsPos(pos)) {
                if (auto const& outputs = redirect->platform.base.outputs; !outputs.empty()) {
                    auto output = base::get_nearest_output(outputs, m_pos.toPoint());
                    QRectF const currentScreenGeometry = output->geometry();
                    pos = confineToBoundingBox(pos, currentScreenGeometry);
                }
            }
        }

        apply_pointer_confinement(pos);
        if (pos == m_pos) {
            // Didn't change due to confinement.
            return;
        }

        // verify screen confinement
        if (!screenContainsPos(pos)) {
            return;
        }

        m_pos = pos;
        Q_EMIT redirect->qobject->globalPointerChanged(m_pos);
    }

    void update_button(button_event const& event)
    {
        m_buttons[event.key] = event.state;

        // update Qt buttons
        qt_buttons = Qt::NoButton;
        for (auto it = m_buttons.constBegin(); it != m_buttons.constEnd(); ++it) {
            if (it.value() == button_state::released) {
                continue;
            }
            qt_buttons |= button_to_qt_mouse_button(it.key());
        }

        Q_EMIT redirect->qobject->pointerButtonStateChanged(event.key, event.state);
    }

    void warp_xcb_on_surface_left(Wrapland::Server::Surface* newSurface)
    {
        auto xc = waylandServer()->xwayland_connection();
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

    void apply_pointer_confinement(QPointF& pos) const
    {
        if (!focus.window) {
            return;
        }

        auto surface = focus.window->surface;
        if (!surface) {
            return;
        }

        auto cf = surface->confinedPointer();
        if (!cf || !cf->isConfined()) {
            return;
        }

        auto const region = getConstraintRegion(focus.window, cf.data());
        if (region.contains(pos.toPoint())) {
            return;
        }

        // Allow either x or y to pass.
        if (auto tmp = QPointF(m_pos.x(), pos.y()); region.contains(tmp.toPoint())) {
            pos = tmp;
            return;
        }
        if (auto tmp = QPointF(pos.x(), m_pos.y()); region.contains(tmp.toPoint())) {
            pos = tmp;
            return;
        }

        pos = m_pos;
    }

    void disconnect_confined_pointer_region_connection()
    {
        QObject::disconnect(notifiers.confined_pointer_region);
        notifiers.confined_pointer_region = QMetaObject::Connection();
    }

    void disconnect_locked_pointer_destroyed_connection()
    {
        QObject::disconnect(notifiers.locked_pointer_destroyed);
        notifiers.locked_pointer_destroyed = QMetaObject::Connection();
    }

    void disconnect_pointer_constraints_connection()
    {
        QObject::disconnect(notifiers.constraints);
        notifiers.constraints = QMetaObject::Connection();

        QObject::disconnect(notifiers.constraints_activated);
        notifiers.constraints_activated = QMetaObject::Connection();
    }

    void break_pointer_constraints(Wrapland::Server::Surface* surface)
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
        disconnect_confined_pointer_region_connection();
        constraints.confined = false;
        constraints.locked = false;
    }

    QPointF m_pos;
    QHash<uint32_t, button_state> m_buttons;
    Qt::MouseButtons qt_buttons;

    struct {
        QMetaObject::Connection focus_geometry;
        QMetaObject::Connection internal_window;
        QMetaObject::Connection constraints;
        QMetaObject::Connection constraints_activated;
        QMetaObject::Connection confined_pointer_region;
        QMetaObject::Connection locked_pointer_destroyed;
        QMetaObject::Connection decoration_geometry;
    } notifiers;

    struct {
        bool confined{false};
        bool locked{false};
        bool enabled{true};
    } constraints;

    motion_scheduler<pointer_redirect> motions;
};

}
