/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"
#include "base/x11/xcb/qt_types.h"

#include <QMouseEvent>

namespace KWin::render::x11
{

template<typename Effects>
class mouse_intercept_filter : public base::x11::event_filter
{
public:
    mouse_intercept_filter(xcb_window_t window, Effects* effects)
        : base::x11::event_filter(
            QVector<int>{XCB_BUTTON_PRESS, XCB_BUTTON_RELEASE, XCB_MOTION_NOTIFY})
        , m_effects(effects)
        , m_window(window)
    {
    }

    bool event(xcb_generic_event_t* event) override
    {
        const uint8_t eventType = event->response_type & ~0x80;
        if (eventType == XCB_BUTTON_PRESS || eventType == XCB_BUTTON_RELEASE) {
            auto* me = reinterpret_cast<xcb_button_press_event_t*>(event);
            if (m_window == me->event) {
                const bool isWheel = me->detail >= 4 && me->detail <= 7;
                if (isWheel) {
                    if (eventType != XCB_BUTTON_PRESS) {
                        return false;
                    }
                    QPoint angleDelta;
                    switch (me->detail) {
                    case 4:
                        angleDelta.setY(120);
                        break;
                    case 5:
                        angleDelta.setY(-120);
                        break;
                    case 6:
                        angleDelta.setX(120);
                        break;
                    case 7:
                        angleDelta.setX(-120);
                        break;
                    }

                    auto const buttons = base::x11::xcb::to_qt_mouse_buttons(me->state);
                    auto const modifiers = base::x11::xcb::to_qt_keyboard_modifiers(me->state);

                    if (modifiers & Qt::AltModifier) {
                        int x = angleDelta.x();
                        int y = angleDelta.y();

                        angleDelta.setX(y);
                        angleDelta.setY(x);
                        // After Qt > 5.14 simplify to
                        // angleDelta = angleDelta.transposed();
                    }

                    if (angleDelta.y()) {
                        QWheelEvent ev(QPoint(me->event_x, me->event_y),
                                       angleDelta.y(),
                                       buttons,
                                       modifiers,
                                       Qt::Vertical);
                        return m_effects->checkInputWindowEvent(&ev);
                    } else if (angleDelta.x()) {
                        QWheelEvent ev(QPoint(me->event_x, me->event_y),
                                       angleDelta.x(),
                                       buttons,
                                       modifiers,
                                       Qt::Horizontal);
                        return m_effects->checkInputWindowEvent(&ev);
                    }
                }
                auto const button = base::x11::xcb::to_qt_mouse_button(me->detail);
                auto buttons = base::x11::xcb::to_qt_mouse_buttons(me->state);
                const QEvent::Type type = (eventType == XCB_BUTTON_PRESS)
                    ? QEvent::MouseButtonPress
                    : QEvent::MouseButtonRelease;
                if (type == QEvent::MouseButtonPress) {
                    buttons |= button;
                } else {
                    buttons &= ~button;
                }
                QMouseEvent ev(type,
                               QPoint(me->event_x, me->event_y),
                               QPoint(me->root_x, me->root_y),
                               button,
                               buttons,
                               base::x11::xcb::to_qt_keyboard_modifiers(me->state));
                return m_effects->checkInputWindowEvent(&ev);
            }
        } else if (eventType == XCB_MOTION_NOTIFY) {
            const auto* me = reinterpret_cast<xcb_motion_notify_event_t*>(event);
            if (m_window == me->event) {
                QMouseEvent ev(QEvent::MouseMove,
                               QPoint(me->event_x, me->event_y),
                               QPoint(me->root_x, me->root_y),
                               Qt::NoButton,
                               base::x11::xcb::to_qt_mouse_buttons(me->state),
                               base::x11::xcb::to_qt_keyboard_modifiers(me->state));
                return m_effects->checkInputWindowEvent(&ev);
            }
        }
        return false;
    }

private:
    Effects* m_effects;
    xcb_window_t m_window;
};

}
