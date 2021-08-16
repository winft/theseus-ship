/*
    SPDX-FileCopyrightText: 2018 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "touch_hide_cursor.h"
#include "main.h"
#include <input/cursor.h>
#include <input/platform.h>

namespace KWin::input
{

void touch_hide_cursor_spy::button([[maybe_unused]] button_event const& event)
{
    showCursor();
}

void touch_hide_cursor_spy::motion([[maybe_unused]] motion_event const& event)
{
    showCursor();
}

void touch_hide_cursor_spy::wheelEvent(WheelEvent* event)
{
    Q_UNUSED(event)
    showCursor();
}

void touch_hide_cursor_spy::touchDown(qint32 id, const QPointF& pos, quint32 time)
{
    Q_UNUSED(id)
    Q_UNUSED(pos)
    Q_UNUSED(time)
    hideCursor();
}

void touch_hide_cursor_spy::showCursor()
{
    if (!m_cursorHidden) {
        return;
    }
    m_cursorHidden = false;
    kwinApp()->input->cursor->show();
}

void touch_hide_cursor_spy::hideCursor()
{
    if (m_cursorHidden) {
        return;
    }
    m_cursorHidden = true;
    kwinApp()->input->cursor->hide();
}

}
