/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "../event_filter.h"

#include <QElapsedTimer>

namespace KWin
{
class Platform;

namespace input
{

class dpms_filter : public event_filter
{
public:
    dpms_filter(Platform* backend);
    bool keyEvent(QKeyEvent* event) override;
    bool pointerEvent(QMouseEvent* event, uint32_t nativeButton) override;
    bool touchDown(int32_t id, const QPointF& pos, uint32_t time) override;
    bool touchMotion(int32_t id, const QPointF& pos, uint32_t time) override;
    bool touchUp(int32_t id, uint32_t time) override;

    bool wheelEvent(QWheelEvent* event) override;

private:
    void notify();

    Platform* m_backend;
    QElapsedTimer m_doubleTapTimer;
    QVector<int32_t> m_touchPoints;
    bool m_secondTap = false;
};

}
}
