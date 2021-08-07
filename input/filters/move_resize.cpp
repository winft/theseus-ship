/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "move_resize.h"

#include "../redirect.h"
#include "main.h"
#include "win/input.h"
#include "win/move.h"
#include "workspace.h"

#include <QKeyEvent>

namespace KWin::input
{

bool move_resize_filter::pointerEvent(QMouseEvent* event, quint32 nativeButton)
{
    Q_UNUSED(nativeButton)
    auto c = workspace()->moveResizeClient();
    if (!c) {
        return false;
    }
    switch (event->type()) {
    case QEvent::MouseMove:
        win::update_move_resize(c, event->screenPos().toPoint());
        break;
    case QEvent::MouseButtonRelease:
        if (event->buttons() == Qt::NoButton) {
            win::end_move_resize(c);
        }
        break;
    default:
        break;
    }
    return true;
}
bool move_resize_filter::wheelEvent(QWheelEvent* event)
{
    Q_UNUSED(event)
    // filter out while moving a window
    return workspace()->moveResizeClient() != nullptr;
}
bool move_resize_filter::keyEvent(QKeyEvent* event)
{
    auto c = workspace()->moveResizeClient();
    if (!c) {
        return false;
    }
    if (event->type() == QEvent::KeyPress) {
        win::key_press_event(c, event->key() | event->modifiers());
        if (win::is_move(c) || win::is_resize(c)) {
            // only update if mode didn't end
            win::update_move_resize(c, kwinApp()->input_redirect->globalPointer());
        }
    }
    return true;
}

bool move_resize_filter::touchDown(qint32 id, const QPointF& pos, quint32 time)
{
    Q_UNUSED(id)
    Q_UNUSED(pos)
    Q_UNUSED(time)
    auto c = workspace()->moveResizeClient();
    if (!c) {
        return false;
    }
    return true;
}

bool move_resize_filter::touchMotion(qint32 id, const QPointF& pos, quint32 time)
{
    Q_UNUSED(time)
    auto c = workspace()->moveResizeClient();
    if (!c) {
        return false;
    }
    if (!m_set) {
        m_id = id;
        m_set = true;
    }
    if (m_id == id) {
        win::update_move_resize(c, pos.toPoint());
    }
    return true;
}

bool move_resize_filter::touchUp(qint32 id, quint32 time)
{
    Q_UNUSED(time)
    auto c = workspace()->moveResizeClient();
    if (!c) {
        return false;
    }
    if (m_id == id || !m_set) {
        win::end_move_resize(c);
        m_set = false;
        // pass through to update decoration filter later on
        return false;
    }
    m_set = false;
    return true;
}

}
