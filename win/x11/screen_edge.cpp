/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
    SPDX-FileCopyrightText: 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "screen_edge.h"

#include "input/cursor.h"

namespace KWin::win::x11
{

screen_edge::screen_edge(win::screen_edger* edger, base::x11::atoms& atoms)
    : win::screen_edge(edger)
    , atoms{atoms}
{
}

screen_edge::~screen_edge()
{
}

void screen_edge::doActivate()
{
    createWindow();
    createApproachWindow();
    doUpdateBlocking();
}

void screen_edge::doDeactivate()
{
    m_window.reset();
    m_approachWindow.reset();
}

void screen_edge::createWindow()
{
    if (m_window.isValid()) {
        return;
    }
    const uint32_t mask = XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
    const uint32_t values[] = {true,
                               XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW
                                   | XCB_EVENT_MASK_POINTER_MOTION};
    m_window.create(geometry, XCB_WINDOW_CLASS_INPUT_ONLY, mask, values);
    m_window.map();
    // Set XdndAware on the windows, so that DND enter events are received (#86998)
    xcb_atom_t version = 4; // XDND version
    xcb_change_property(connection(),
                        XCB_PROP_MODE_REPLACE,
                        m_window,
                        atoms.xdnd_aware,
                        XCB_ATOM_ATOM,
                        32,
                        1,
                        (unsigned char*)(&version));
}

void screen_edge::createApproachWindow()
{
    if (!activatesForPointer()) {
        return;
    }
    if (m_approachWindow.isValid()) {
        return;
    }
    if (!approach_geometry.isValid()) {
        return;
    }
    const uint32_t mask = XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
    const uint32_t values[] = {true,
                               XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW
                                   | XCB_EVENT_MASK_POINTER_MOTION};
    m_approachWindow.create(approach_geometry, XCB_WINDOW_CLASS_INPUT_ONLY, mask, values);
    m_approachWindow.map();
}

void screen_edge::doGeometryUpdate()
{
    m_window.setGeometry(geometry);
    if (m_approachWindow.isValid()) {
        m_approachWindow.setGeometry(approach_geometry);
    }
}

void screen_edge::doStartApproaching()
{
    if (!activatesForPointer()) {
        return;
    }
    m_approachWindow.unmap();
    auto cursor = input::get_cursor();
#ifndef KWIN_UNIT_TEST
    m_cursorPollingConnection
        = connect(cursor, &input::cursor::pos_changed, this, &screen_edge::updateApproaching);
#endif
    cursor->start_mouse_polling();
}

void screen_edge::doStopApproaching()
{
    if (!m_cursorPollingConnection) {
        return;
    }
    disconnect(m_cursorPollingConnection);
    m_cursorPollingConnection = QMetaObject::Connection();
    input::get_cursor()->stop_mouse_polling();
    m_approachWindow.map();
}

void screen_edge::doUpdateBlocking()
{
    if (reserved_count == 0) {
        return;
    }
    if (is_blocked) {
        m_window.unmap();
        m_approachWindow.unmap();
    } else {
        m_window.map();
        m_approachWindow.map();
    }
}

}
