/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window_selector.h"

#include "base/x11/grabs.h"
#include "base/x11/xcb/proto.h"
#include "input/cursor.h"
#include "win/space.h"
#include "win/x11/window.h"
#include "win/x11/window_find.h"

#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <fixx11h.h>

#include <xcb/xcb_keysyms.h>

namespace KWin::input::x11
{

window_selector::window_selector()
    : base::x11::event_filter(QVector<int>{XCB_BUTTON_PRESS,
                                           XCB_BUTTON_RELEASE,
                                           XCB_MOTION_NOTIFY,
                                           XCB_ENTER_NOTIFY,
                                           XCB_LEAVE_NOTIFY,
                                           XCB_KEY_PRESS,
                                           XCB_KEY_RELEASE,
                                           XCB_FOCUS_IN,
                                           XCB_FOCUS_OUT})
    , m_active(false)
{
}

window_selector::~window_selector()
{
}

void window_selector::start(std::function<void(KWin::Toplevel*)> callback,
                            const QByteArray& cursorName)
{
    if (m_active) {
        callback(nullptr);
        return;
    }

    m_active = activate(cursorName);
    if (!m_active) {
        callback(nullptr);
        return;
    }
    m_callback = callback;
}

void window_selector::start(std::function<void(const QPoint&)> callback)
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

bool window_selector::activate(const QByteArray& cursorName)
{
    xcb_cursor_t cursor = createCursor(cursorName);
    xcb_connection_t* c = connection();

    auto cookie = xcb_grab_pointer_unchecked(
        c,
        false,
        rootWindow(),
        XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION
            | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW,
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

xcb_cursor_t window_selector::createCursor(const QByteArray& cursorName)
{
    if (cursorName.isEmpty()) {
        return input::get_cursor()->x11_cursor(Qt::CrossCursor);
    }
    auto cursor = input::get_cursor()->x11_cursor(cursorName);
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
        const xcb_font_t cursorFont = xcb_generate_id(c);
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

void window_selector::processEvent(xcb_generic_event_t* event)
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

bool window_selector::event(xcb_generic_event_t* event)
{
    if (!m_active) {
        return false;
    }
    processEvent(event);

    return true;
}

void window_selector::handleButtonRelease(xcb_button_t button, xcb_window_t window)
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
            m_pointSelectionFallback(input::get_cursor()->pos());
        }
        release();
        return;
    }
}

void window_selector::handleKeyPress(xcb_keycode_t keycode, uint16_t state)
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

    auto cursor = input::get_cursor();
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

void window_selector::selectWindowUnderPointer()
{
    base::x11::xcb::pointer pointer(rootWindow());
    if (!pointer.is_null() && pointer->child != XCB_WINDOW_NONE) {
        selectWindowId(pointer->child);
    }
}

void window_selector::release()
{
    base::x11::ungrab_keyboard();
    xcb_ungrab_pointer(connection(), XCB_TIME_CURRENT_TIME);
    base::x11::ungrab_server();
    m_active = false;
    m_callback = std::function<void(KWin::Toplevel*)>();
    m_pointSelectionFallback = std::function<void(const QPoint&)>();
}

void window_selector::selectWindowId(xcb_window_t window_to_select)
{
    if (window_to_select == XCB_WINDOW_NONE) {
        m_callback(nullptr);
        return;
    }
    xcb_window_t window = window_to_select;
    win::x11::window* client = nullptr;
    while (true) {
        client = win::x11::find_controlled_window<win::x11::window>(
            *workspace(), win::x11::predicate_match::frame_id, window);
        if (client) {
            break; // Found the client
        }
        base::x11::xcb::tree tree(window);
        if (window == tree->root) {
            // We didn't find the client, probably an override-redirect window
            break;
        }
        window = tree->parent; // Go up
    }
    if (client) {
        m_callback(client);
    } else {
        m_callback(workspace()->findUnmanaged(window_to_select));
    }
}

void window_selector::cancelCallback()
{
    if (m_callback) {
        m_callback(nullptr);
    } else if (m_pointSelectionFallback) {
        m_pointSelectionFallback(QPoint(-1, -1));
    }
}

} // namespace
