/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "device_redirect.h"

#include "base/wayland/server.h"
#include "input/event_filter.h"
#include "input/event_spy.h"
#include "input/touch_redirect.h"
#include "main.h"

#include <KScreenLocker/KsldApp>
#include <QHash>
#include <QPointF>
#include <Wrapland/Server/drag_pool.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/touch_pool.h>

namespace KWin::input::wayland
{

template<typename Redirect>
class touch_redirect : public input::touch_redirect<Redirect>
{
public:
    using base_t = typename Redirect::platform_t::base_t;
    using space_t = typename base_t::space_t;

    explicit touch_redirect(Redirect* redirect)
        : input::touch_redirect<Redirect>(redirect)
    {
    }

    void init()
    {
        device_redirect_init(this);

        if (waylandServer()->has_screen_locker_integration()) {
            QObject::connect(ScreenLocker::KSldApp::self(),
                             &ScreenLocker::KSldApp::lockStateChanged,
                             this->qobject.get(),
                             [this] {
                                 if (!waylandServer()->seat()->hasTouch()) {
                                     return;
                                 }
                                 cancel();
                                 // position doesn't matter
                                 device_redirect_update(this);
                             });
        }
    }

    QPointF position() const override
    {
        return m_lastPosition;
    }

    bool positionValid() const override
    {
        assert(m_touches >= 0);

        // We can only determine a position with at least one touch point.
        return m_touches;
    }

    void process_down(touch_down_event const& event) override
    {
        auto const event_abs = touch_down_event({event.id,
                                                 get_abs_pos(event.pos, event.base.dev),
                                                 {event.base.dev, event.base.time_msec}});

        m_lastPosition = event_abs.pos;
        window_already_updated_this_cycle = false;
        m_touches++;
        if (m_touches == 1) {
            device_redirect_update(this);
        }
        process_spies(
            this->redirect->m_spies,
            std::bind(&event_spy<Redirect>::touch_down, std::placeholders::_1, event_abs));
        process_filters(this->redirect->m_filters,
                        std::bind(&input::event_filter<Redirect>::touch_down,
                                  std::placeholders::_1,
                                  event_abs));
        window_already_updated_this_cycle = false;
    }

    void process_up(touch_up_event const& event) override
    {
        window_already_updated_this_cycle = false;

        process_spies(this->redirect->m_spies,
                      std::bind(&event_spy<Redirect>::touch_up, std::placeholders::_1, event));
        process_filters(
            this->redirect->m_filters,
            std::bind(&input::event_filter<Redirect>::touch_up, std::placeholders::_1, event));

        window_already_updated_this_cycle = false;
        m_touches--;

        if (m_touches == 0) {
            device_redirect_update(this);
        }
    }

    void process_motion(touch_motion_event const& event) override
    {
        auto const event_abs = touch_motion_event({event.id,
                                                   get_abs_pos(event.pos, event.base.dev),
                                                   {event.base.dev, event.base.time_msec}});

        m_lastPosition = event_abs.pos;
        window_already_updated_this_cycle = false;

        process_spies(
            this->redirect->m_spies,
            std::bind(&event_spy<Redirect>::touch_motion, std::placeholders::_1, event_abs));
        process_filters(this->redirect->m_filters,
                        std::bind(&input::event_filter<Redirect>::touch_motion,
                                  std::placeholders::_1,
                                  event_abs));

        window_already_updated_this_cycle = false;
    }

    bool focusUpdatesBlocked() override
    {
        if (window_already_updated_this_cycle) {
            return true;
        }

        window_already_updated_this_cycle = true;

        if (waylandServer()->seat()->drags().is_touch_drag()) {
            return true;
        }
        if (m_touches > 1) {
            // first touch defines focus
            return true;
        }

        return false;
    }

    void cancel() override
    {
        if (!waylandServer()->seat()->hasTouch()) {
            return;
        }
        waylandServer()->seat()->touches().cancel_sequence();
        m_idMapper.clear();
    }

    void frame() override
    {
        waylandServer()->seat()->touches().touch_frame();
    }

    void insertId(qint32 internalId, qint32 wraplandId) override
    {
        m_idMapper.insert(internalId, wraplandId);
    }

    void removeId(qint32 internalId) override
    {
        m_idMapper.remove(internalId);
    }

    qint32 mappedId(qint32 internalId) override
    {
        auto it = m_idMapper.constFind(internalId);
        if (it != m_idMapper.constEnd()) {
            return it.value();
        }
        return -1;
    }

    void setDecorationPressId(qint32 id) override
    {
        m_decorationId = id;
    }

    qint32 decorationPressId() const override
    {
        return m_decorationId;
    }

    void setInternalPressId(qint32 id) override
    {
        m_internalId = id;
    }

    qint32 internalPressId() const override
    {
        return m_internalId;
    }

    void cleanupInternalWindow(QWindow* /*old*/, QWindow* /*now*/) override
    {
        // nothing to do
    }

    void cleanupDecoration(win::deco::client_impl<typename space_t::window_t>* /*old*/,
                           win::deco::client_impl<typename space_t::window_t>* /*now*/) override
    {
        // nothing to do
    }

    void focusUpdate(typename space_t::window_t* focusOld,
                     typename space_t::window_t* focusNow) override
    {
        // TODO: handle pointer grab aka popups

        if (focusOld && focusOld->control) {
            win::leave_event(focusOld);
        }

        QObject::disconnect(focus_geometry_notifier);
        focus_geometry_notifier = QMetaObject::Connection();

        if (focusNow && focusNow->control) {
            win::enter_event(focusNow, m_lastPosition.toPoint());
            this->redirect->space.focusMousePos = m_lastPosition.toPoint();
        }

        auto seat = waylandServer()->seat();
        if (!focusNow || !focusNow->surface || this->focus.deco) {
            // no new surface or internal window or on decoration -> cleanup
            seat->touches().set_focused_surface(nullptr);
            return;
        }

        // TODO(romangg): Invalidate pointer focus?

        // TODO(romangg): Add input transformation API to Wrapland::Server::Seat for touch input.
        seat->touches().set_focused_surface(
            focusNow->surface,
            -1 * win::get_input_transform(*focusNow).map(focusNow->geo.pos())
                + focusNow->geo.pos());
        focus_geometry_notifier = QObject::connect(
            focusNow->qobject.get(),
            &win::window_qobject::frame_geometry_changed,
            this->qobject.get(),
            [this] {
                auto focus_win = this->focus.window;
                if (!focus_win) {
                    return;
                }
                auto seat = waylandServer()->seat();
                if (focus_win->surface != seat->touches().get_focus().surface) {
                    return;
                }
                seat->touches().set_focused_surface_position(
                    -1 * win::get_input_transform(*focus_win).map(focus_win->geo.pos())
                    + focus_win->geo.pos());
            });
    }

private:
    QPointF get_abs_pos(QPointF const& pos, touch* dev)
    {
        auto dev_impl = static_cast<touch_impl<base_t>*>(dev);
        auto out = dev_impl->output;

        if (!out) {
            auto const& outs = this->redirect->platform.base.outputs;
            if (outs.empty()) {
                return {};
            }
            out = outs.front();
        }

        auto const& geo = out->geometry();

        return QPointF(geo.x() + geo.width() * pos.x(), geo.y() + geo.height() * pos.y());
    }

    qint32 m_decorationId = -1;
    qint32 m_internalId = -1;

    /**
     * external/wrapland
     */
    QHash<qint32, qint32> m_idMapper;
    QMetaObject::Connection focus_geometry_notifier;
    bool window_already_updated_this_cycle = false;
    QPointF m_lastPosition;

    int m_touches = 0;
};

}
