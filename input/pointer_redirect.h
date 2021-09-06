/*
    SPDX-FileCopyrightText: 2013, 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "device_redirect.h"
#include "event.h"
#include "redirect.h"

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <memory>

class QWindow;

namespace Wrapland::Server
{
class Surface;
}

namespace KWin
{
class Toplevel;

namespace Decoration
{
class DecoratedClientImpl;
}

namespace input
{
class pointer;

class KWIN_EXPORT pointer_redirect : public device_redirect
{
    Q_OBJECT
public:
    static bool s_cursorUpdateBlocking;

    pointer_redirect();
    ~pointer_redirect() override;

    void init() override;

    void updateAfterScreenChange();
    bool supportsWarping() const;
    void warp(const QPointF& pos);

    QPointF pos() const
    {
        return m_pos;
    }
    Qt::MouseButtons buttons() const
    {
        return m_qtButtons;
    }
    bool areButtonsPressed() const;

    void setEffectsOverrideCursor(Qt::CursorShape shape);
    void removeEffectsOverrideCursor();
    void setWindowSelectionCursor(const QByteArray& shape);
    void removeWindowSelectionCursor();

    void updatePointerConstraints();

    void setEnableConstraints(bool set);

    bool isConstrained() const
    {
        return m_confined || m_locked;
    }

    bool focusUpdatesBlocked() override;

    void process_motion(motion_event const& event);
    void process_motion_absolute(motion_absolute_event const& event);
    void processMotion(const QPointF& pos, uint32_t time, KWin::input::pointer* device = nullptr);

    void process_button(button_event const& event);
    void process_axis(axis_event const& event);

    void process_swipe_begin(swipe_begin_event const& event);
    void
    processSwipeGestureBegin(int fingerCount, quint32 time, KWin::input::pointer* device = nullptr);
    void process_swipe_update(swipe_update_event const& event);
    void processSwipeGestureUpdate(const QSizeF& delta,
                                   quint32 time,
                                   KWin::input::pointer* device = nullptr);
    void process_swipe_end(swipe_end_event const& event);
    void processSwipeGestureEnd(quint32 time, KWin::input::pointer* device = nullptr);
    void processSwipeGestureCancelled(quint32 time, KWin::input::pointer* device = nullptr);

    void process_pinch_begin(pinch_begin_event const& event);
    void process_pinch_update(pinch_update_event const& event);
    void process_pinch_end(pinch_end_event const& event);

private:
    void cleanupInternalWindow(QWindow* old, QWindow* now) override;
    void cleanupDecoration(Decoration::DecoratedClientImpl* old,
                           Decoration::DecoratedClientImpl* now) override;

    void focusUpdate(Toplevel* focusOld, Toplevel* focusNow) override;

    QPointF position() const override;

    void updateOnStartMoveResize();
    void updateToReset();
    void updatePosition(const QPointF& pos);
    void update_button(button_event const& event);
    void warpXcbOnSurfaceLeft(Wrapland::Server::Surface* surface);
    QPointF applyPointerConfinement(const QPointF& pos) const;
    void disconnectConfinedPointerRegionConnection();
    void disconnectLockedPointerDestroyedConnection();
    void disconnectPointerConstraintsConnection();
    void breakPointerConstraints(Wrapland::Server::Surface* surface);
    bool m_supportsWarping;
    QPointF m_pos;
    QHash<uint32_t, input::redirect::PointerButtonState> m_buttons;
    Qt::MouseButtons m_qtButtons;
    QMetaObject::Connection m_focusGeometryConnection;
    QMetaObject::Connection m_internalWindowConnection;
    QMetaObject::Connection m_constraintsConnection;
    QMetaObject::Connection m_constraintsActivatedConnection;
    QMetaObject::Connection m_confinedPointerRegionConnection;
    QMetaObject::Connection m_lockedPointerDestroyedConnection;
    QMetaObject::Connection m_decorationGeometryConnection;
    bool m_confined = false;
    bool m_locked = false;
    bool m_enableConstraints = true;
};

}
}
