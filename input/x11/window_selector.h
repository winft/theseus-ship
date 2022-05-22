/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"

#include <functional>
#include <xcb/xcb.h>

class QPoint;

namespace KWin
{
class Toplevel;

namespace input::x11
{

class platform;

class window_selector : public base::x11::event_filter
{
public:
    window_selector(x11::platform& platform);
    ~window_selector() override;

    void start(std::function<void(KWin::Toplevel*)> callback, const QByteArray& cursorName);
    void start(std::function<void(const QPoint&)> callback);
    bool isActive() const
    {
        return m_active;
    }
    void processEvent(xcb_generic_event_t* event);

    bool event(xcb_generic_event_t* event) override;

private:
    xcb_cursor_t createCursor(const QByteArray& cursorName);
    void release();
    void selectWindowUnderPointer();
    void handleKeyPress(xcb_keycode_t keycode, uint16_t state);
    void handleButtonRelease(xcb_button_t button, xcb_window_t window);
    void selectWindowId(xcb_window_t window_to_kill);
    bool activate(const QByteArray& cursorName = QByteArray());
    void cancelCallback();

    bool m_active{false};
    std::function<void(KWin::Toplevel*)> m_callback;
    std::function<void(const QPoint&)> m_pointSelectionFallback;
    x11::platform& platform;
};

}
}
