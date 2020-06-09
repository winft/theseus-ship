/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>
Copyright 2020 Roman Gilg <subdiff@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#pragma once

#include "input.h"

#include <QElapsedTimer>

namespace KWin
{

class Platform;

class DpmsInputEventFilter : public InputEventFilter
{
public:
    DpmsInputEventFilter(Platform *backend);

    bool pointerEvent(QMouseEvent *event, uint32_t nativeButton) override;
    bool wheelEvent(QWheelEvent *event) override;
    bool keyEvent(QKeyEvent *event) override;
    bool touchDown(int32_t id, const QPointF &pos, uint32_t time) override;
    bool touchMotion(int32_t id, const QPointF &pos, uint32_t time) override;
    bool touchUp(int32_t id, uint32_t time) override;

private:
    void notify();
    Platform *m_backend;
    QElapsedTimer m_doubleTapTimer;
    QVector<int32_t> m_touchPoints;
    bool m_secondTap = false;
};

}
