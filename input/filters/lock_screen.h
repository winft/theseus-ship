/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "../event_filter.h"

namespace Wrapland::Server
{
class Seat;
class Surface;
}

namespace KWin::input
{

class lock_screen_filter : public event_filter
{
public:
    bool pointerEvent(QMouseEvent* event, quint32 nativeButton) override;
    bool wheelEvent(QWheelEvent* event) override;
    bool keyEvent(QKeyEvent* event) override;
    bool touchDown(qint32 id, const QPointF& pos, quint32 time) override;
    bool touchMotion(qint32 id, const QPointF& pos, quint32 time) override;
    bool touchUp(qint32 id, quint32 time) override;
    bool pinchGestureBegin(int fingerCount, quint32 time) override;
    bool
    pinchGestureUpdate(qreal scale, qreal angleDelta, const QSizeF& delta, quint32 time) override;
    bool pinchGestureEnd(quint32 time) override;
    bool pinchGestureCancelled(quint32 time) override;

    bool swipeGestureBegin(int fingerCount, quint32 time) override;
    bool swipeGestureUpdate(const QSizeF& delta, quint32 time) override;
    bool swipeGestureEnd(quint32 time) override;
    bool swipeGestureCancelled(quint32 time) override;

private:
    bool surfaceAllowed(Wrapland::Server::Surface* (Wrapland::Server::Seat::*method)() const) const;
    bool pointerSurfaceAllowed() const;
    bool keyboardSurfaceAllowed() const;
    bool touchSurfaceAllowed() const;
};

}
