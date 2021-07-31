/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <QKeyEvent>

namespace KWin
{
class SwitchEvent;

namespace input
{

/**
 * Base class for filtering input events inside InputRedirection.
 *
 * The idea behind the event_filter is to have task oriented
 * filters. E.g. there is one filter taking care of a locked screen,
 * one to take care of interacting with window decorations, etc.
 *
 * A concrete subclass can reimplement the virtual methods and decide
 * whether an event should be filtered out or not by returning either
 * @c true or @c false. E.g. the lock screen filter can easily ensure
 * that all events are filtered out.
 *
 * As soon as a filter returns @c true the processing is stopped. If
 * a filter returns @c false the next one is invoked. This means a filter
 * installed early gets to see more events than a filter installed later on.
 *
 * Deleting an instance of event_filter automatically uninstalls it from
 * InputRedirection.
 */
class KWIN_EXPORT event_filter
{
public:
    event_filter();
    virtual ~event_filter();

    /**
     * Event filter for pointer events which can be described by a QMouseEvent.
     *
     * Please note that the button translation in QMouseEvent cannot cover all
     * possible buttons. Because of that also the @p nativeButton code is passed
     * through the filter. For internal areas it's fine to use @p event, but for
     * passing to client windows the @p nativeButton should be used.
     *
     * @param event The event information about the move or button press/release
     * @param nativeButton The native key code of the button, for move events 0
     * @return @c true to stop further event processing, @c false to pass to next filter
     */
    virtual bool pointerEvent(QMouseEvent* event, quint32 nativeButton);
    /**
     * Event filter for pointer axis events.
     *
     * @param event The event information about the axis event
     * @return @c true to stop further event processing, @c false to pass to next filter
     */
    virtual bool wheelEvent(QWheelEvent* event);
    /**
     * Event filter for keyboard events.
     *
     * @param event The event information about the key event
     * @return @c tru to stop further event processing, @c false to pass to next filter.
     */
    virtual bool keyEvent(QKeyEvent* event);
    virtual bool touchDown(qint32 id, const QPointF& pos, quint32 time);
    virtual bool touchMotion(qint32 id, const QPointF& pos, quint32 time);
    virtual bool touchUp(qint32 id, quint32 time);

    virtual bool pinchGestureBegin(int fingerCount, quint32 time);
    virtual bool
    pinchGestureUpdate(qreal scale, qreal angleDelta, const QSizeF& delta, quint32 time);
    virtual bool pinchGestureEnd(quint32 time);
    virtual bool pinchGestureCancelled(quint32 time);

    virtual bool swipeGestureBegin(int fingerCount, quint32 time);
    virtual bool swipeGestureUpdate(const QSizeF& delta, quint32 time);
    virtual bool swipeGestureEnd(quint32 time);
    virtual bool swipeGestureCancelled(quint32 time);

    virtual bool switchEvent(SwitchEvent* event);

    virtual bool tabletToolEvent(QTabletEvent* event);
    virtual bool tabletToolButtonEvent(const QSet<uint>& buttons);
    virtual bool tabletPadButtonEvent(const QSet<uint>& buttons);
    virtual bool tabletPadStripEvent(int number, int position, bool isFinger);
    virtual bool tabletPadRingEvent(int number, int position, bool isFinger);

protected:
    void passToWaylandServer(QKeyEvent* event);
};

}
}
