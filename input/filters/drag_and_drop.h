/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input.h"

namespace KWin::input
{

class drag_and_drop_filter : public InputEventFilter
{
public:
    bool pointerEvent(QMouseEvent* event, quint32 nativeButton) override;
    bool touchDown(qint32 id, const QPointF& pos, quint32 time) override;
    bool touchMotion(qint32 id, const QPointF& pos, quint32 time) override;
    bool touchUp(qint32 id, quint32 time) override;

private:
    qint32 m_touchId = -1;
};

}
