/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "decoration_event.h"

#include "helpers.h"
#include "input/pointer_redirect.h"
#include "input/touch_redirect.h"
#include "main.h"
#include "wayland_server.h"
#include "win/deco.h"
#include "win/input.h"

#include <QKeyEvent>

namespace KWin::input
{

bool decoration_event_filter::pointerEvent(QMouseEvent* event, quint32 nativeButton)
{
    Q_UNUSED(nativeButton)
    auto decoration = kwinApp()->input_redirect->pointer()->decoration();
    if (!decoration) {
        return false;
    }
    const QPointF p = event->globalPos() - decoration->client()->pos();
    switch (event->type()) {
    case QEvent::MouseMove: {
        QHoverEvent e(QEvent::HoverMove, p, p);
        QCoreApplication::instance()->sendEvent(decoration->decoration(), &e);
        win::process_decoration_move(decoration->client(), p.toPoint(), event->globalPos());
        return true;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease: {
        const auto actionResult = perform_client_mouse_action(event, decoration->client());
        if (actionResult.first) {
            return actionResult.second;
        }
        QMouseEvent e(event->type(),
                      p,
                      event->globalPos(),
                      event->button(),
                      event->buttons(),
                      event->modifiers());
        e.setAccepted(false);
        QCoreApplication::sendEvent(decoration->decoration(), &e);
        if (!e.isAccepted() && event->type() == QEvent::MouseButtonPress) {
            win::process_decoration_button_press(decoration->client(), &e, false);
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            win::process_decoration_button_release(decoration->client(), &e);
        }
        return true;
    }
    default:
        break;
    }
    return false;
}

bool decoration_event_filter::wheelEvent(QWheelEvent* event)
{
    auto decoration = kwinApp()->input_redirect->pointer()->decoration();
    if (!decoration) {
        return false;
    }
    if (event->angleDelta().y() != 0) {
        // client window action only on vertical scrolling
        const auto actionResult = perform_client_wheel_action(event, decoration->client());
        if (actionResult.first) {
            return actionResult.second;
        }
    }
    const QPointF localPos = event->globalPosF() - decoration->client()->pos();
    const Qt::Orientation orientation
        = (event->angleDelta().x() != 0) ? Qt::Horizontal : Qt::Vertical;
    const int delta
        = event->angleDelta().x() != 0 ? event->angleDelta().x() : event->angleDelta().y();
    QWheelEvent e(localPos,
                  event->globalPosF(),
                  QPoint(),
                  event->angleDelta(),
                  delta,
                  orientation,
                  event->buttons(),
                  event->modifiers());
    e.setAccepted(false);
    QCoreApplication::sendEvent(decoration, &e);
    if (e.isAccepted()) {
        return true;
    }
    if ((orientation == Qt::Vertical)
        && win::titlebar_positioned_under_mouse(decoration->client())) {
        decoration->client()->performMouseCommand(options->operationTitlebarMouseWheel(delta * -1),
                                                  event->globalPosF().toPoint());
    }
    return true;
}

bool decoration_event_filter::touchDown(qint32 id, const QPointF& pos, quint32 time)
{
    auto seat = waylandServer()->seat();
    if (seat->isTouchSequence()) {
        return false;
    }
    if (kwinApp()->input_redirect->touch()->decorationPressId() != -1) {
        // already on a decoration, ignore further touch points, but filter out
        return true;
    }
    seat->setTimestamp(time);
    auto decoration = kwinApp()->input_redirect->touch()->decoration();
    if (!decoration) {
        return false;
    }

    kwinApp()->input_redirect->touch()->setDecorationPressId(id);
    m_lastGlobalTouchPos = pos;
    m_lastLocalTouchPos = pos - decoration->client()->pos();

    QHoverEvent hoverEvent(QEvent::HoverMove, m_lastLocalTouchPos, m_lastLocalTouchPos);
    QCoreApplication::sendEvent(decoration->decoration(), &hoverEvent);

    QMouseEvent e(QEvent::MouseButtonPress,
                  m_lastLocalTouchPos,
                  pos,
                  Qt::LeftButton,
                  Qt::LeftButton,
                  kwinApp()->input_redirect->keyboardModifiers());
    e.setAccepted(false);
    QCoreApplication::sendEvent(decoration->decoration(), &e);
    if (!e.isAccepted()) {
        win::process_decoration_button_press(decoration->client(), &e, false);
    }
    return true;
}

bool decoration_event_filter::touchMotion(qint32 id, const QPointF& pos, quint32 time)
{
    Q_UNUSED(time)
    auto decoration = kwinApp()->input_redirect->touch()->decoration();
    if (!decoration) {
        return false;
    }
    if (kwinApp()->input_redirect->touch()->decorationPressId() == -1) {
        return false;
    }
    if (kwinApp()->input_redirect->touch()->decorationPressId() != qint32(id)) {
        // ignore, but filter out
        return true;
    }
    m_lastGlobalTouchPos = pos;
    m_lastLocalTouchPos = pos - decoration->client()->pos();

    QHoverEvent e(QEvent::HoverMove, m_lastLocalTouchPos, m_lastLocalTouchPos);
    QCoreApplication::instance()->sendEvent(decoration->decoration(), &e);
    win::process_decoration_move(
        decoration->client(), m_lastLocalTouchPos.toPoint(), pos.toPoint());
    return true;
}

bool decoration_event_filter::touchUp(qint32 id, quint32 time)
{
    Q_UNUSED(time);
    auto decoration = kwinApp()->input_redirect->touch()->decoration();
    if (!decoration) {
        return false;
    }
    if (kwinApp()->input_redirect->touch()->decorationPressId() == -1) {
        return false;
    }
    if (kwinApp()->input_redirect->touch()->decorationPressId() != qint32(id)) {
        // ignore, but filter out
        return true;
    }

    // send mouse up
    QMouseEvent e(QEvent::MouseButtonRelease,
                  m_lastLocalTouchPos,
                  m_lastGlobalTouchPos,
                  Qt::LeftButton,
                  Qt::MouseButtons(),
                  kwinApp()->input_redirect->keyboardModifiers());
    e.setAccepted(false);
    QCoreApplication::sendEvent(decoration->decoration(), &e);
    win::process_decoration_button_release(decoration->client(), &e);

    QHoverEvent leaveEvent(QEvent::HoverLeave, QPointF(), QPointF());
    QCoreApplication::sendEvent(decoration->decoration(), &leaveEvent);

    m_lastGlobalTouchPos = QPointF();
    m_lastLocalTouchPos = QPointF();
    kwinApp()->input_redirect->touch()->setDecorationPressId(-1);
    return true;
}

}
