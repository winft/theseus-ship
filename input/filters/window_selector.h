/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "helpers.h"

#include "base/wayland/server.h"
#include "input/event.h"
#include "input/event_filter.h"
#include "input/keyboard.h"
#include "input/keyboard_redirect.h"
#include "input/pointer_redirect.h"
#include "input/qt_event.h"
#include "input/window_find.h"
#include "input/xkb/keyboard.h"
#include "main.h"

#include <Wrapland/Server/seat.h>
#include <linux/input.h>

namespace KWin::input
{

template<typename Redirect>
class window_selector_filter : public event_filter<Redirect>
{
public:
    explicit window_selector_filter(Redirect& redirect)
        : event_filter<Redirect>(redirect)
    {
    }

    bool button(button_event const& event) override
    {
        if (!m_active) {
            return false;
        }

        auto& pointer = this->redirect.pointer;
        if (event.state == button_state::released) {
            if (pointer->buttons() == Qt::NoButton) {
                if (event.key == BTN_RIGHT) {
                    cancel();
                } else {
                    accept(pointer->pos());
                }
            }
        }

        return true;
    }

    bool motion(motion_event const& /*event*/) override
    {
        return m_active;
    }

    bool axis(axis_event const& /*event*/) override
    {
        return m_active;
    }

    bool key(key_event const& event) override
    {
        if (!m_active) {
            return false;
        }

        waylandServer()->seat()->setFocusedKeyboardSurface(nullptr);
        pass_to_wayland_server(event);

        if (event.state == key_state::pressed) {
            auto const qt_key = key_to_qt_key(event.keycode, event.base.dev->xkb.get());

            // x11 variant does this on key press, so do the same
            if (qt_key == Qt::Key_Escape) {
                cancel();
            } else if (qt_key == Qt::Key_Enter || qt_key == Qt::Key_Return
                       || qt_key == Qt::Key_Space) {
                accept(this->redirect.globalPointer());
            }

            int mx = 0;
            int my = 0;
            if (qt_key == Qt::Key_Left) {
                mx = -10;
            }
            if (qt_key == Qt::Key_Right) {
                mx = 10;
            }
            if (qt_key == Qt::Key_Up) {
                my = -10;
            }
            if (qt_key == Qt::Key_Down) {
                my = 10;
            }
            if (event.base.dev->xkb->qt_modifiers & Qt::ControlModifier) {
                mx /= 10;
                my /= 10;
            }

            auto const pos = this->redirect.globalPointer() + QPointF(mx, my);
            this->redirect.warp_pointer(pos, event.base.time_msec);
        }
        // filter out while selecting a window
        return true;
    }

    bool key_repeat(key_event const& /*event*/) override
    {
        return m_active;
    }

    bool touch_down(touch_down_event const& event) override
    {
        if (!isActive()) {
            return false;
        }
        m_touchPoints.insert(event.id, event.pos);
        return true;
    }

    bool touch_motion(touch_motion_event const& event) override
    {
        if (!isActive()) {
            return false;
        }
        auto it = m_touchPoints.find(event.id);
        if (it != m_touchPoints.end()) {
            *it = event.pos;
        }
        return true;
    }

    bool touch_up(touch_up_event const& event) override
    {
        if (!isActive()) {
            return false;
        }
        auto it = m_touchPoints.find(event.id);
        if (it != m_touchPoints.end()) {
            const auto pos = it.value();
            m_touchPoints.erase(it);
            if (m_touchPoints.isEmpty()) {
                accept(pos);
            }
        }
        return true;
    }

    bool isActive() const
    {
        return m_active;
    }

    void start(std::function<void(typename Redirect::window_t*)> callback)
    {
        Q_ASSERT(!m_active);
        m_active = true;
        m_callback = callback;
        this->redirect.keyboard->update();
        this->redirect.cancelTouch();
    }

    void start(std::function<void(const QPoint&)> callback)
    {
        Q_ASSERT(!m_active);
        m_active = true;
        m_pointSelectionFallback = callback;
        this->redirect.keyboard->update();
        this->redirect.cancelTouch();
    }

private:
    void deactivate()
    {
        m_active = false;
        m_callback = std::function<void(typename Redirect::window_t*)>();
        m_pointSelectionFallback = std::function<void(const QPoint&)>();
        this->redirect.pointer->removeWindowSelectionCursor();
        this->redirect.keyboard->update();
        m_touchPoints.clear();
    }

    void cancel()
    {
        if (m_callback) {
            m_callback(nullptr);
        }
        if (m_pointSelectionFallback) {
            m_pointSelectionFallback(QPoint(-1, -1));
        }
        deactivate();
    }

    void accept(const QPoint& pos)
    {
        if (m_callback) {
            // TODO: this ignores shaped windows
            m_callback(find_window(this->redirect, pos));
        }
        if (m_pointSelectionFallback) {
            m_pointSelectionFallback(pos);
        }
        deactivate();
    }

    void accept(const QPointF& pos)
    {
        accept(pos.toPoint());
    }

    bool m_active = false;
    std::function<void(typename Redirect::window_t*)> m_callback;
    std::function<void(const QPoint&)> m_pointSelectionFallback;
    QMap<quint32, QPointF> m_touchPoints;
};

}
