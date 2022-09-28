/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "device_redirect.h"

#include "input/device_redirect.h"
#include "input/event_filter.h"
#include "input/event_spy.h"

#include <QPointF>
#include <QSet>

namespace KWin::input::wayland
{

template<typename Redirect>
class tablet_redirect
{
public:
    using space_t = typename Redirect::platform_t::base_t::space_t;
    using window_t = typename space_t::window_t;

    explicit tablet_redirect(Redirect* redirect)
        : qobject{std::make_unique<device_redirect_qobject>()}
        , redirect{redirect}
    {
    }

    void init()
    {
        device_redirect_init(this);
    }

    QPointF position() const
    {
        return last_position;
    }

    bool positionValid() const
    {
        return !last_position.isNull();
    }

    void tabletToolEvent(TabletEventType type,
                         QPointF const& pos,
                         qreal pressure,
                         int x_tilt,
                         int y_tilt,
                         qreal rotation,
                         bool tip_down,
                         bool tip_near,
                         quint64 serial_id,
                         quint64 /*toolId*/,
                         void* /*device*/)
    {
        last_position = pos;

        auto t = QEvent::None;
        switch (type) {
        case TabletEventType::Axis:
            t = QEvent::TabletMove;
            break;
        case TabletEventType::Tip:
            t = tip_down ? QEvent::TabletPress : QEvent::TabletRelease;
            break;
        case TabletEventType::Proximity:
            t = tip_near ? QEvent::TabletEnterProximity : QEvent::TabletLeaveProximity;
            break;
        }

        auto const button = tip.down ? Qt::LeftButton : Qt::NoButton;
        QTabletEvent ev(t,
                        pos,
                        pos,
                        QTabletEvent::Stylus,
                        QTabletEvent::Pen,
                        pressure,
                        x_tilt,
                        y_tilt,
                        0, // tangentialPressure
                        rotation,
                        0, // z
                        Qt::NoModifier,
                        serial_id,
                        button,
                        button);

        process_spies(redirect->m_spies,
                      std::bind(&event_spy<Redirect>::tabletToolEvent, std::placeholders::_1, &ev));
        process_filters(
            redirect->m_filters,
            std::bind(&input::event_filter<Redirect>::tabletToolEvent, std::placeholders::_1, &ev));

        tip.down = tip_down;
        tip.near = tip_near;
    }

    void tabletToolButtonEvent(uint button, bool isPressed)
    {
        if (isPressed) {
            pressed_buttons.tool.insert(button);
        } else {
            pressed_buttons.tool.remove(button);
        }

        process_spies(redirect->m_spies,
                      std::bind(&event_spy<Redirect>::tabletToolButtonEvent,
                                std::placeholders::_1,
                                pressed_buttons.tool));
        process_filters(redirect->m_filters,
                        std::bind(&input::event_filter<Redirect>::tabletToolButtonEvent,
                                  std::placeholders::_1,
                                  pressed_buttons.tool));
    }

    void tabletPadButtonEvent(uint button, bool isPressed)
    {
        if (isPressed) {
            pressed_buttons.pad.insert(button);
        } else {
            pressed_buttons.pad.remove(button);
        }

        process_spies(redirect->m_spies,
                      std::bind(&event_spy<Redirect>::tabletPadButtonEvent,
                                std::placeholders::_1,
                                pressed_buttons.pad));
        process_filters(redirect->m_filters,
                        std::bind(&input::event_filter<Redirect>::tabletPadButtonEvent,
                                  std::placeholders::_1,
                                  pressed_buttons.pad));
    }

    void tabletPadStripEvent(int number, int position, bool is_finger)
    {
        process_spies(redirect->m_spies,
                      std::bind(&event_spy<Redirect>::tabletPadStripEvent,
                                std::placeholders::_1,
                                number,
                                position,
                                is_finger));
        process_filters(redirect->m_filters,
                        std::bind(&input::event_filter<Redirect>::tabletPadStripEvent,
                                  std::placeholders::_1,
                                  number,
                                  position,
                                  is_finger));
    }

    void tabletPadRingEvent(int number, int position, bool is_finger)
    {
        process_spies(redirect->m_spies,
                      std::bind(&event_spy<Redirect>::tabletPadRingEvent,
                                std::placeholders::_1,
                                number,
                                position,
                                is_finger));
        process_filters(redirect->m_filters,
                        std::bind(&input::event_filter<Redirect>::tabletPadRingEvent,
                                  std::placeholders::_1,
                                  number,
                                  position,
                                  is_finger));
    }

    void cleanupDecoration(win::deco::client_impl<typename space_t::window_t>* /*old*/,
                           win::deco::client_impl<typename space_t::window_t>* /*now*/)
    {
    }

    void cleanupInternalWindow(QWindow* /*old*/, QWindow* /*now*/)
    {
    }

    void focusUpdate(typename space_t::window_t* /*old*/, typename space_t::window_t* /*now*/)
    {
    }

    std::unique_ptr<device_redirect_qobject> qobject;
    Redirect* redirect;

    device_redirect_at<window_t> at;
    device_redirect_focus<window_t> focus;

private:
    struct {
        bool down = false;
        bool near = false;
    } tip;

    QPointF last_position;

    struct {
        QSet<uint> tool;
        QSet<uint> pad;
    } pressed_buttons;
};

}
