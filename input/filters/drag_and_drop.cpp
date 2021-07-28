/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "drag_and_drop.h"

#include "../touch_redirect.h"
#include "main.h"
#include "seat/session.h"
#include "toplevel.h"
#include "wayland_server.h"
#include "workspace.h"
#include "xkb.h"
#include "xwl/xwayland_interface.h"

#include <Wrapland/Server/seat.h>

#include <QKeyEvent>

namespace KWin::input
{

bool drag_and_drop_filter::pointerEvent(QMouseEvent* event, quint32 nativeButton)
{
    auto seat = waylandServer()->seat();
    if (!seat->isDragPointer()) {
        return false;
    }
    if (seat->isDragTouch()) {
        return true;
    }
    seat->setTimestamp(event->timestamp());
    switch (event->type()) {
    case QEvent::MouseMove: {
        auto const pos = kwinApp()->input_redirect->globalPointer();
        seat->setPointerPos(pos);

        const auto eventPos = event->globalPos();
        // TODO: use InputDeviceHandler::at() here and check isClient()?
        Toplevel* t = kwinApp()->input_redirect->findManagedToplevel(eventPos);
        if (auto* xwl = xwayland()) {
            const auto ret = xwl->dragMoveFilter(t, eventPos);
            if (ret == Xwl::DragEventReply::Ignore) {
                return false;
            } else if (ret == Xwl::DragEventReply::Take) {
                break;
            }
        }

        if (t) {
            // TODO: consider decorations
            if (t->surface() != seat->dragSurface()) {
                if (t->control) {
                    workspace()->activateClient(t);
                }
                seat->setDragTarget(t->surface(), t->input_transform());
            }
        } else {
            // no window at that place, if we have a surface we need to reset
            seat->setDragTarget(nullptr);
        }
        break;
    }
    case QEvent::MouseButtonPress:
        seat->pointerButtonPressed(nativeButton);
        break;
    case QEvent::MouseButtonRelease:
        seat->pointerButtonReleased(nativeButton);
        break;
    default:
        break;
    }
    // TODO: should we pass through effects?
    return true;
}

bool drag_and_drop_filter::touchDown(qint32 id, const QPointF& pos, quint32 time)
{
    auto seat = waylandServer()->seat();
    if (seat->isDragPointer()) {
        return true;
    }
    if (!seat->isDragTouch()) {
        return false;
    }
    if (m_touchId != id) {
        return true;
    }
    seat->setTimestamp(time);
    kwinApp()->input_redirect->touch()->insertId(id, seat->touchDown(pos));
    return true;
}
bool drag_and_drop_filter::touchMotion(qint32 id, const QPointF& pos, quint32 time)
{
    auto seat = waylandServer()->seat();
    if (seat->isDragPointer()) {
        return true;
    }
    if (!seat->isDragTouch()) {
        return false;
    }
    if (m_touchId < 0) {
        // We take for now the first id appearing as a move after a drag
        // started. We can optimize by specifying the id the drag is
        // associated with by implementing a key-value getter in Wrapland.
        m_touchId = id;
    }
    if (m_touchId != id) {
        return true;
    }
    seat->setTimestamp(time);
    const qint32 wraplandId = kwinApp()->input_redirect->touch()->mappedId(id);
    if (wraplandId == -1) {
        return true;
    }

    seat->touchMove(wraplandId, pos);

    if (Toplevel* t = kwinApp()->input_redirect->findToplevel(pos.toPoint())) {
        // TODO: consider decorations
        if (t->surface() != seat->dragSurface()) {
            if (t->control) {
                workspace()->activateClient(t);
            }
            seat->setDragTarget(t->surface(), pos, t->input_transform());
        }
    } else {
        // no window at that place, if we have a surface we need to reset
        seat->setDragTarget(nullptr);
    }
    return true;
}
bool drag_and_drop_filter::touchUp(qint32 id, quint32 time)
{
    auto seat = waylandServer()->seat();
    if (!seat->isDragTouch()) {
        return false;
    }
    seat->setTimestamp(time);
    const qint32 wraplandId = kwinApp()->input_redirect->touch()->mappedId(id);
    if (wraplandId != -1) {
        seat->touchUp(wraplandId);
        kwinApp()->input_redirect->touch()->removeId(id);
    }
    if (m_touchId == id) {
        m_touchId = -1;
    }
    return true;
}

}
