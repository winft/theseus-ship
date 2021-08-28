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
#include <input/event.h>
#include <input/pointer_redirect.h>
#include <input/qt_event.h>

#include <QKeyEvent>

namespace KWin::input
{

bool move_resize_filter::button([[maybe_unused]] button_event const& event)
{
    auto window = workspace()->moveResizeClient();
    if (!window) {
        return false;
    }
    if (kwinApp()->input->redirect->pointer()->buttons() == Qt::NoButton) {
        win::end_move_resize(window);
    }
    return true;
}

bool move_resize_filter::motion([[maybe_unused]] motion_event const& event)
{
    auto window = workspace()->moveResizeClient();
    if (!window) {
        return false;
    }
    auto pos = kwinApp()->input->redirect->globalPointer();
    win::update_move_resize(window, pos.toPoint());
    return true;
}

bool move_resize_filter::axis([[maybe_unused]] axis_event const& event)
{
    return workspace()->moveResizeClient() != nullptr;
}

void process_key_press(Toplevel* window, QKeyEvent* event)
{
    win::key_press_event(window, event->key() | event->modifiers());

    if (win::is_move(window) || win::is_resize(window)) {
        // Only update if mode didn't end.
        win::update_move_resize(window, kwinApp()->input->redirect->globalPointer());
    }
}

bool move_resize_filter::keyEvent(QKeyEvent* event)
{
    auto window = workspace()->moveResizeClient();
    if (!window) {
        return false;
    }
    if (event->type() == QEvent::KeyPress) {
        process_key_press(window, event);
    }
    return true;
}

bool move_resize_filter::key_repeat(QKeyEvent* event)
{
    auto window = workspace()->moveResizeClient();
    if (!window) {
        return false;
    }

    process_key_press(window, event);
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
