/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/touch_redirect.h"

#include <QHash>
#include <QPointF>

namespace KWin
{

class Toplevel;

namespace input
{
class touch;

namespace wayland
{

class KWIN_EXPORT touch_redirect : public input::touch_redirect
{
    Q_OBJECT
public:
    explicit touch_redirect(input::redirect* redirect);

    void init();

    QPointF position() const override;
    bool positionValid() const override;

    void process_down(touch_down_event const& event) override;
    void process_up(touch_up_event const& event) override;
    void process_motion(touch_motion_event const& event) override;

    bool focusUpdatesBlocked() override;

    void cancel() override;
    void frame() override;

    void insertId(qint32 internalId, qint32 wraplandId) override;
    void removeId(qint32 internalId) override;
    qint32 mappedId(qint32 internalId) override;

    void setDecorationPressId(qint32 id) override;
    qint32 decorationPressId() const override;
    void setInternalPressId(qint32 id) override;
    qint32 internalPressId() const override;

    void cleanupInternalWindow(QWindow* old, QWindow* now) override;
    void cleanupDecoration(win::deco::client_impl* old, win::deco::client_impl* now) override;

    void focusUpdate(Toplevel* focusOld, Toplevel* focusNow) override;

private:
    qint32 m_decorationId = -1;
    qint32 m_internalId = -1;

    /**
     * external/wrapland
     */
    QHash<qint32, qint32> m_idMapper;
    QMetaObject::Connection focus_geometry_notifier;
    bool window_already_updated_this_cycle = false;
    QPointF m_lastPosition;

    int m_touches = 0;
};

}
}
}
