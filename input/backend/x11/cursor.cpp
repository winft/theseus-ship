/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "cursor.h"
#include "utils.h"
#include "xcbutils.h"
#include "xfixes_cursor_event_filter.h"

#include <QAbstractEventDispatcher>
#include <QTimer>

#include <xcb/xcb_cursor.h>

namespace KWin::input::backend::x11
{

cursor::cursor(bool xInputSupport)
    : input::cursor()
    , m_timeStamp(XCB_TIME_CURRENT_TIME)
    , m_buttonMask(0)
    , m_resetTimeStampTimer(new QTimer(this))
    , m_mousePollingTimer(new QTimer(this))
    , m_hasXInput(xInputSupport)
    , m_needsPoll(false)
{
    m_resetTimeStampTimer->setSingleShot(true);
    connect(m_resetTimeStampTimer, &QTimer::timeout, this, &cursor::resetTimeStamp);
    // TODO: How often do we really need to poll?
    m_mousePollingTimer->setInterval(50);
    connect(m_mousePollingTimer, &QTimer::timeout, this, &cursor::mousePolled);

    connect(this, &input::cursor::themeChanged, this, [this] { m_cursors.clear(); });

    if (m_hasXInput) {
        connect(qApp->eventDispatcher(),
                &QAbstractEventDispatcher::aboutToBlock,
                this,
                &cursor::aboutToBlock);
    }

#ifndef KCMRULES
    connect(kwinApp(), &Application::workspaceCreated, this, [this] {
        if (Xcb::Extensions::self()->isFixesAvailable()) {
            m_xfixesFilter = std::make_unique<xfixes_cursor_event_filter>(this);
        }
    });
#endif
}

cursor::~cursor()
{
}

void cursor::doSetPos()
{
    const QPoint& pos = currentPos();
    xcb_warp_pointer(connection(), XCB_WINDOW_NONE, rootWindow(), 0, 0, 0, 0, pos.x(), pos.y());
    // call default implementation to emit signal
    input::cursor::doSetPos();
}

void cursor::doGetPos()
{
    if (m_timeStamp != XCB_TIME_CURRENT_TIME && m_timeStamp == xTime()) {
        // time stamps did not change, no need to query again
        return;
    }
    m_timeStamp = xTime();
    Xcb::Pointer pointer(rootWindow());
    if (pointer.isNull()) {
        return;
    }
    m_buttonMask = pointer->mask;
    updatePos(pointer->root_x, pointer->root_y);
    m_resetTimeStampTimer->start(0);
}

void cursor::resetTimeStamp()
{
    m_timeStamp = XCB_TIME_CURRENT_TIME;
}

void cursor::aboutToBlock()
{
    if (m_needsPoll) {
        mousePolled();
        m_needsPoll = false;
    }
}

void cursor::doStartMousePolling()
{
    if (!m_hasXInput) {
        m_mousePollingTimer->start();
    }
}

void cursor::doStopMousePolling()
{
    if (!m_hasXInput) {
        m_mousePollingTimer->stop();
    }
}

void cursor::doStartCursorTracking()
{
    xcb_xfixes_select_cursor_input(
        connection(), rootWindow(), XCB_XFIXES_CURSOR_NOTIFY_MASK_DISPLAY_CURSOR);
}

void cursor::doStopCursorTracking()
{
    xcb_xfixes_select_cursor_input(connection(), rootWindow(), 0);
}

void cursor::mousePolled()
{
    static QPoint lastPos = currentPos();
    static uint16_t lastMask = m_buttonMask;
    doGetPos(); // Update if needed
    if (lastPos != currentPos() || lastMask != m_buttonMask) {
        emit mouseChanged(currentPos(),
                          lastPos,
                          x11ToQtMouseButtons(m_buttonMask),
                          x11ToQtMouseButtons(lastMask),
                          x11ToQtKeyboardModifiers(m_buttonMask),
                          x11ToQtKeyboardModifiers(lastMask));
        lastPos = currentPos();
        lastMask = m_buttonMask;
    }
}

xcb_cursor_t cursor::getX11Cursor(input::cursor_shape shape)
{
    return getX11Cursor(shape.name());
}

xcb_cursor_t cursor::getX11Cursor(const QByteArray& name)
{
    auto it = m_cursors.constFind(name);
    if (it != m_cursors.constEnd()) {
        return it.value();
    }
    return createCursor(name);
}

xcb_cursor_t cursor::createCursor(const QByteArray& name)
{
    if (name.isEmpty()) {
        return XCB_CURSOR_NONE;
    }
    xcb_cursor_context_t* ctx;
    if (xcb_cursor_context_new(connection(), defaultScreen(), &ctx) < 0) {
        return XCB_CURSOR_NONE;
    }
    xcb_cursor_t cursor = xcb_cursor_load_cursor(ctx, name.constData());
    if (cursor == XCB_CURSOR_NONE) {
        const auto& names = cursorAlternativeNames(name);
        for (auto cit = names.begin(); cit != names.end(); ++cit) {
            cursor = xcb_cursor_load_cursor(ctx, (*cit).constData());
            if (cursor != XCB_CURSOR_NONE) {
                break;
            }
        }
    }
    if (cursor != XCB_CURSOR_NONE) {
        m_cursors.insert(name, cursor);
    }
    xcb_cursor_context_free(ctx);
    return cursor;
}

void cursor::notifyCursorChanged()
{
    if (!isCursorTracking()) {
        // cursor change tracking is currently disabled, so don't emit signal
        return;
    }
    emit cursorChanged();
}

}
