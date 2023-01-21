/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"
#include "base/x11/grabs.h"
#include "base/x11/xcb/proto.h"
#include "kwinglobals.h"
#include "win/x11/unmanaged.h"
#include "win/x11/window_find.h"

#include <QPoint>
#include <functional>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

// Needs to be included before Xutil, because XLib whose macros collide with Qt declarations in
// QDBus, in particular the "True" and "False" names.
#include <QtCore>

#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <fixx11h.h>

namespace KWin::input::x11
{

template<typename Redirect>
class window_selector : public base::x11::event_filter
{
public:
    using window_t = typename Redirect::space_t::window_t;
    using x11_window_t = typename Redirect::space_t::x11_window;

    window_selector(Redirect& redirect)
        : base::x11::event_filter(QVector<int>{XCB_BUTTON_PRESS,
                                               XCB_BUTTON_RELEASE,
                                               XCB_MOTION_NOTIFY,
                                               XCB_ENTER_NOTIFY,
                                               XCB_LEAVE_NOTIFY,
                                               XCB_KEY_PRESS,
                                               XCB_KEY_RELEASE,
                                               XCB_FOCUS_IN,
                                               XCB_FOCUS_OUT})
        , redirect{redirect}
    {
    }

    void start(std::function<void(std::optional<window_t>)> callback, QByteArray const& cursorName)
    {
        if (m_active) {
            callback({});
            return;
        }

        m_active = activate(cursorName);
        if (!m_active) {
            callback({});
            return;
        }
        m_callback = callback;
    }

    void start(std::function<void(QPoint const&)> callback)
    {
        if (m_active) {
            callback(QPoint(-1, -1));
            return;
        }

        m_active = activate();
        if (!m_active) {
            callback(QPoint(-1, -1));
            return;
        }
        m_pointSelectionFallback = callback;
    }

    bool isActive() const
    {
        return m_active;
    }
    void processEvent(xcb_generic_event_t* event)
    {
        if (event->response_type == XCB_BUTTON_RELEASE) {
            xcb_button_release_event_t* buttonEvent
                = reinterpret_cast<xcb_button_release_event_t*>(event);
            handleButtonRelease(buttonEvent->detail, buttonEvent->child);
        } else if (event->response_type == XCB_KEY_PRESS) {
            xcb_key_press_event_t* keyEvent = reinterpret_cast<xcb_key_press_event_t*>(event);
            handleKeyPress(keyEvent->detail, keyEvent->state);
        }
    }

    bool event(xcb_generic_event_t* event) override
    {
        if (!m_active) {
            return false;
        }
        processEvent(event);

        return true;
    }

private:
    xcb_cursor_t createCursor(QByteArray const& cursorName)
    {
        if (cursorName.isEmpty()) {
            return redirect.cursor->x11_cursor(Qt::CrossCursor);
        }
        auto cursor = redirect.cursor->x11_cursor(cursorName);
        if (cursor != XCB_CURSOR_NONE) {
            return cursor;
        }
        if (cursorName == QByteArrayLiteral("pirate")) {
            // special handling for font pirate cursor
            static xcb_cursor_t kill_cursor = XCB_CURSOR_NONE;
            if (kill_cursor != XCB_CURSOR_NONE) {
                return kill_cursor;
            }
            // fallback on font
            xcb_connection_t* c = connection();
            xcb_font_t const cursorFont = xcb_generate_id(c);
            xcb_open_font(c, cursorFont, strlen("cursor"), "cursor");
            cursor = xcb_generate_id(c);
            xcb_create_glyph_cursor(c,
                                    cursor,
                                    cursorFont,
                                    cursorFont,
                                    XC_pirate,     /* source character glyph */
                                    XC_pirate + 1, /* mask character glyph */
                                    0,
                                    0,
                                    0,
                                    0,
                                    0,
                                    0); /* r b g r b g */
            kill_cursor = cursor;
        }
        return cursor;
    }

    void release()
    {
        base::x11::ungrab_keyboard();
        xcb_ungrab_pointer(connection(), XCB_TIME_CURRENT_TIME);
        base::x11::ungrab_server();
        m_active = false;
        m_callback = {};
        m_pointSelectionFallback = std::function<void(const QPoint&)>();
    }

    void selectWindowUnderPointer()
    {
        auto const& x11_data = redirect.platform.base.x11_data;
        base::x11::xcb::pointer pointer(x11_data.connection, x11_data.root_window);
        if (!pointer.is_null() && pointer->child != XCB_WINDOW_NONE) {
            selectWindowId(pointer->child);
        }
    }

    void handleKeyPress(xcb_keycode_t keycode, uint16_t state)
    {
        xcb_key_symbols_t* symbols = xcb_key_symbols_alloc(connection());
        xcb_keysym_t kc = xcb_key_symbols_get_keysym(symbols, keycode, 0);
        int mx = 0;
        int my = 0;
        const bool returnPressed = (kc == XK_Return) || (kc == XK_space);
        const bool escapePressed = (kc == XK_Escape);
        if (kc == XK_Left) {
            mx = -10;
        }
        if (kc == XK_Right) {
            mx = 10;
        }
        if (kc == XK_Up) {
            my = -10;
        }
        if (kc == XK_Down) {
            my = 10;
        }
        if (state & XCB_MOD_MASK_CONTROL) {
            mx /= 10;
            my /= 10;
        }

        auto& cursor = redirect.cursor;
        cursor->set_pos(cursor->pos() + QPoint(mx, my));

        if (returnPressed) {
            if (m_callback) {
                selectWindowUnderPointer();
            } else if (m_pointSelectionFallback) {
                m_pointSelectionFallback(cursor->pos());
            }
        }

        if (returnPressed || escapePressed) {
            if (escapePressed) {
                cancelCallback();
            }
            release();
        }
        xcb_key_symbols_free(symbols);
    }

    void handleButtonRelease(xcb_button_t button, xcb_window_t window)
    {
        if (button == XCB_BUTTON_INDEX_3) {
            cancelCallback();
            release();
            return;
        }
        if (button == XCB_BUTTON_INDEX_1 || button == XCB_BUTTON_INDEX_2) {
            if (m_callback) {
                selectWindowId(window);
            } else if (m_pointSelectionFallback) {
                m_pointSelectionFallback(redirect.cursor->pos());
            }
            release();
            return;
        }
    }

    void selectWindowId(xcb_window_t window_to_select)
    {
        if (window_to_select == XCB_WINDOW_NONE) {
            m_callback(nullptr);
            return;
        }

        xcb_window_t window = window_to_select;
        x11_window_t* client = nullptr;

        while (true) {
            client = win::x11::find_controlled_window<x11_window_t>(
                redirect.space, win::x11::predicate_match::frame_id, window);
            if (client) {
                break; // Found the client
            }
            base::x11::xcb::tree tree(redirect.platform.base.x11_data.connection, window);
            if (window == tree->root) {
                // We didn't find the client, probably an override-redirect window
                break;
            }
            window = tree->parent; // Go up
        }
        if (client) {
            m_callback(client);
        } else {
            m_callback(win::x11::find_unmanaged<x11_window_t>(redirect.space, window_to_select));
        }
    }

    bool activate(const QByteArray& cursorName = QByteArray())
    {
        xcb_cursor_t cursor = createCursor(cursorName);
        xcb_connection_t* c = connection();

        auto cookie = xcb_grab_pointer_unchecked(
            c,
            false,
            rootWindow(),
            XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
                | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_ENTER_WINDOW
                | XCB_EVENT_MASK_LEAVE_WINDOW,
            XCB_GRAB_MODE_ASYNC,
            XCB_GRAB_MODE_ASYNC,
            XCB_WINDOW_NONE,
            cursor,
            XCB_TIME_CURRENT_TIME);

        unique_cptr<xcb_grab_pointer_reply_t> reply(xcb_grab_pointer_reply(c, cookie, nullptr));
        if (!reply || reply->status != XCB_GRAB_STATUS_SUCCESS) {
            return false;
        }

        const bool grabbed = base::x11::grab_keyboard();
        if (grabbed) {
            base::x11::grab_server();
        } else {
            xcb_ungrab_pointer(connection(), XCB_TIME_CURRENT_TIME);
        }
        return grabbed;
    }

    void cancelCallback()
    {
        if (m_callback) {
            m_callback(nullptr);
        } else if (m_pointSelectionFallback) {
            m_pointSelectionFallback(QPoint(-1, -1));
        }
    }

    bool m_active{false};
    std::function<void(std::optional<window_t>)> m_callback;
    std::function<void(const QPoint&)> m_pointSelectionFallback;
    Redirect& redirect;
};

}
