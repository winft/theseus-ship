/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "event.h"
#include <kwin_export.h>

#include <QtGlobal>

class QPointF;
class QSizeF;
class QTabletEvent;

namespace KWin::input
{
class MouseEvent;
class WheelEvent;
class SwitchEvent;

/**
 * Base class for spying on input events inside InputRedirection.
 *
 * This class is quite similar to InputEventFilter, except that it does not
 * support event filtering. Each event_spy gets to see all input events,
 * the processing happens prior to sending events through the InputEventFilters.
 *
 * Deleting an instance of event_spy automatically uninstalls it from
 * InputRedirection.
 */
class KWIN_EXPORT event_spy
{
public:
    event_spy();
    virtual ~event_spy();

    virtual void button(button_event const& event);
    virtual void motion(motion_event const& event);
    virtual void axis(axis_event const& event);

    virtual void key(key_event const& event);
    virtual void key_repeat(key_event const& event);

    virtual void touchDown(qint32 id, const QPointF& pos, quint32 time);
    virtual void touchMotion(qint32 id, const QPointF& pos, quint32 time);
    virtual void touchUp(qint32 id, quint32 time);

    virtual void pinch_begin(pinch_begin_event const& event);
    virtual void pinch_update(pinch_update_event const& event);
    virtual void pinch_end(pinch_end_event const& event);

    virtual void swipe_begin(swipe_begin_event const& event);
    virtual void swipeGestureUpdate(const QSizeF& delta, quint32 time);
    virtual void swipeGestureEnd(quint32 time);
    virtual void swipeGestureCancelled(quint32 time);

    virtual void switchEvent(SwitchEvent* event);

    virtual void tabletToolEvent(QTabletEvent* event);
    virtual void tabletToolButtonEvent(const QSet<uint>& pressedButtons);
    virtual void tabletPadButtonEvent(const QSet<uint>& pressedButtons);
    virtual void tabletPadStripEvent(int number, int position, bool isFinger);
    virtual void tabletPadRingEvent(int number, int position, bool isFinger);
};

}
