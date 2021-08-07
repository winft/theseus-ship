/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "cursor.h"

#include <kwinglobals.h>

#include <QObject>
#include <QPointF>

namespace KWin::input
{

/**
 * @brief Implementation using the InputRedirection framework to get pointer positions.
 *
 * Does not support warping of cursor.
 */
class cursor_redirect : public cursor
{
    Q_OBJECT
public:
    explicit cursor_redirect(QObject* parent);
    ~cursor_redirect() override;

protected:
    void doSetPos() override;
    void doStartCursorTracking() override;
    void doStopCursorTracking() override;
private Q_SLOTS:
    void slotPosChanged(const QPointF& pos);
    void slotPointerButtonChanged();
    void slotModifiersChanged(Qt::KeyboardModifiers mods, Qt::KeyboardModifiers oldMods);

private:
    Qt::MouseButtons m_currentButtons;
    friend class cursor;
};

}
