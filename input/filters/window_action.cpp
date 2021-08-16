/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "window_action.h"

#include "../pointer_redirect.h"
#include "../touch_redirect.h"
#include "helpers.h"
#include "main.h"
#include "toplevel.h"
#include "wayland_server.h"
#include "win/input.h"
#include "win/transient.h"

#include <Wrapland/Server/seat.h>

#include <QKeyEvent>

namespace KWin::input
{

bool window_action_filter::pointerEvent(QMouseEvent* event, quint32 nativeButton)
{
    Q_UNUSED(nativeButton)
    if (event->type() != QEvent::MouseButtonPress) {
        return false;
    }
    auto focus_window = get_focus_lead(kwinApp()->input->redirect->pointer()->focus());
    if (!focus_window) {
        return false;
    }

    const auto actionResult
        = perform_client_mouse_action(event, focus_window, MouseAction::ModifierAndWindow);
    if (actionResult.first) {
        return actionResult.second;
    }
    return false;
}

bool window_action_filter::wheelEvent(QWheelEvent* event)
{
    if (event->angleDelta().y() == 0) {
        // only actions on vertical scroll
        return false;
    }
    auto focus_window = get_focus_lead(kwinApp()->input->redirect->pointer()->focus());
    if (!focus_window) {
        return false;
    }
    const auto actionResult
        = perform_client_wheel_action(event, focus_window, MouseAction::ModifierAndWindow);
    if (actionResult.first) {
        return actionResult.second;
    }
    return false;
}

bool window_action_filter::touchDown(qint32 id, const QPointF& pos, quint32 time)
{
    Q_UNUSED(id)
    Q_UNUSED(time)
    auto seat = waylandServer()->seat();
    if (seat->isTouchSequence()) {
        return false;
    }
    auto focus_window = get_focus_lead(kwinApp()->input->redirect->touch()->focus());
    if (!focus_window) {
        return false;
    }
    bool wasAction = false;
    auto const command = win::get_mouse_command(focus_window, Qt::LeftButton, &wasAction);
    if (wasAction) {
        return !focus_window->performMouseCommand(command, pos.toPoint());
    }
    return false;
}

Toplevel* window_action_filter::get_focus_lead(Toplevel* focus)
{
    if (!focus) {
        return nullptr;
    }
    focus = win::lead_of_annexed_transient(focus);
    if (!focus->control) {
        return nullptr;
    }
    return focus;
}

}
