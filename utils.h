/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QPoint>
#include <QRect>
#include <climits>

namespace KWin
{

QPoint const invalidPoint(INT_MIN, INT_MIN);

enum StrutArea {
    StrutAreaInvalid = 0, // Null
    StrutAreaTop = 1 << 0,
    StrutAreaRight = 1 << 1,
    StrutAreaBottom = 1 << 2,
    StrutAreaLeft = 1 << 3,
    StrutAreaAll = StrutAreaTop | StrutAreaRight | StrutAreaBottom | StrutAreaLeft
};
Q_DECLARE_FLAGS(StrutAreas, StrutArea)

class StrutRect : public QRect
{
public:
    explicit StrutRect(QRect rect = QRect(), StrutArea area = StrutAreaInvalid)
        : QRect(rect)
        , m_area(area)
    {
    }

    StrutRect(StrutRect const& other)
        : QRect(other)
        , m_area(other.area())
    {
    }

    StrutRect& operator=(StrutRect const& other)
    {
        if (this != &other) {
            QRect::operator=(other);
            m_area = other.area();
        }
        return *this;
    }

    inline StrutArea area() const
    {
        return m_area;
    }

private:
    StrutArea m_area;
};

using StrutRects = QVector<StrutRect>;

}

Q_DECLARE_OPERATORS_FOR_FLAGS(KWin::StrutAreas)
