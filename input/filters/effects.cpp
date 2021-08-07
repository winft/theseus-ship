/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "effects.h"

#include "../../effects.h"
#include "wayland_server.h"

#include <Wrapland/Server/seat.h>

#include <QKeyEvent>

namespace KWin::input
{

bool effects_filter::pointerEvent(QMouseEvent* event, quint32 nativeButton)
{
    Q_UNUSED(nativeButton)
    if (!effects) {
        return false;
    }
    return static_cast<EffectsHandlerImpl*>(effects)->checkInputWindowEvent(event);
}

bool effects_filter::wheelEvent(QWheelEvent* event)
{
    if (!effects) {
        return false;
    }
    return static_cast<EffectsHandlerImpl*>(effects)->checkInputWindowEvent(event);
}

bool effects_filter::keyEvent(QKeyEvent* event)
{
    if (!effects || !static_cast<EffectsHandlerImpl*>(effects)->hasKeyboardGrab()) {
        return false;
    }
    waylandServer()->seat()->setFocusedKeyboardSurface(nullptr);
    passToWaylandServer(event);
    static_cast<EffectsHandlerImpl*>(effects)->grabbedKeyboardEvent(event);
    return true;
}

bool effects_filter::touchDown(qint32 id, const QPointF& pos, quint32 time)
{
    if (!effects) {
        return false;
    }
    return static_cast<EffectsHandlerImpl*>(effects)->touchDown(id, pos, time);
}

bool effects_filter::touchMotion(qint32 id, const QPointF& pos, quint32 time)
{
    if (!effects) {
        return false;
    }
    return static_cast<EffectsHandlerImpl*>(effects)->touchMotion(id, pos, time);
}

bool effects_filter::touchUp(qint32 id, quint32 time)
{
    if (!effects) {
        return false;
    }
    return static_cast<EffectsHandlerImpl*>(effects)->touchUp(id, time);
}

}
