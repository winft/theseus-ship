/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window_selector.h"

#include "helpers.h"

#include "base/wayland/server.h"
#include "input/event.h"
#include "input/keyboard.h"
#include "input/keyboard_redirect.h"
#include "input/pointer_redirect.h"
#include "input/qt_event.h"
#include "input/redirect.h"
#include "input/wayland/platform.h"
#include "input/xkb/keyboard.h"
#include "main.h"

#include <Wrapland/Server/seat.h>

#include <linux/input.h>

namespace KWin::input
{

bool window_selector_filter::button(button_event const& event)
{
    if (!m_active) {
        return false;
    }

    auto pointer = kwinApp()->input->redirect->pointer();
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

bool window_selector_filter::motion([[maybe_unused]] motion_event const& event)
{
    return m_active;
}

bool window_selector_filter::axis([[maybe_unused]] axis_event const& event)
{
    return m_active;
}

bool window_selector_filter::key(key_event const& event)
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
        } else if (qt_key == Qt::Key_Enter || qt_key == Qt::Key_Return || qt_key == Qt::Key_Space) {
            accept(kwinApp()->input->redirect->globalPointer());
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

        auto platform = static_cast<input::wayland::platform*>(kwinApp()->input.get());
        auto const pos = kwinApp()->input->redirect->globalPointer() + QPointF(mx, my);

        platform->warp_pointer(pos, event.base.time_msec);
    }
    // filter out while selecting a window
    return true;
}

bool window_selector_filter::key_repeat(key_event const& /*event*/)
{
    return m_active;
}

bool window_selector_filter::touch_down(touch_down_event const& event)
{
    if (!isActive()) {
        return false;
    }
    m_touchPoints.insert(event.id, event.pos);
    return true;
}

bool window_selector_filter::touch_motion(touch_motion_event const& event)
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

bool window_selector_filter::touch_up(touch_up_event const& event)
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

bool window_selector_filter::isActive() const
{
    return m_active;
}

void window_selector_filter::start(std::function<void(KWin::Toplevel*)> callback)
{
    Q_ASSERT(!m_active);
    m_active = true;
    m_callback = callback;
    kwinApp()->input->redirect->keyboard()->update();
    kwinApp()->input->redirect->cancelTouch();
}

void window_selector_filter::start(std::function<void(const QPoint&)> callback)
{
    Q_ASSERT(!m_active);
    m_active = true;
    m_pointSelectionFallback = callback;
    kwinApp()->input->redirect->keyboard()->update();
    kwinApp()->input->redirect->cancelTouch();
}

void window_selector_filter::deactivate()
{
    m_active = false;
    m_callback = std::function<void(KWin::Toplevel*)>();
    m_pointSelectionFallback = std::function<void(const QPoint&)>();
    kwinApp()->input->redirect->pointer()->removeWindowSelectionCursor();
    kwinApp()->input->redirect->keyboard()->update();
    m_touchPoints.clear();
}

void window_selector_filter::cancel()
{
    if (m_callback) {
        m_callback(nullptr);
    }
    if (m_pointSelectionFallback) {
        m_pointSelectionFallback(QPoint(-1, -1));
    }
    deactivate();
}

void window_selector_filter::accept(const QPoint& pos)
{
    if (m_callback) {
        // TODO: this ignores shaped windows
        m_callback(kwinApp()->input->redirect->findToplevel(pos));
    }
    if (m_pointSelectionFallback) {
        m_pointSelectionFallback(pos);
    }
    deactivate();
}

void window_selector_filter::accept(const QPointF& pos)
{
    accept(pos.toPoint());
}

}
