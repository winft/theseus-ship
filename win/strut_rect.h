/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QRect>

namespace KWin::win
{

enum StrutArea {
    StrutAreaInvalid = 0, // Null
    StrutAreaTop = 1 << 0,
    StrutAreaRight = 1 << 1,
    StrutAreaBottom = 1 << 2,
    StrutAreaLeft = 1 << 3,
    StrutAreaAll = StrutAreaTop | StrutAreaRight | StrutAreaBottom | StrutAreaLeft
};
Q_DECLARE_FLAGS(StrutAreas, StrutArea)

class strut_rect : public QRect
{
public:
    explicit strut_rect(QRect rect = QRect(), StrutArea area = StrutAreaInvalid)
        : QRect(rect)
        , m_area(area)
    {
    }

    strut_rect(strut_rect const& other)
        : QRect(other)
        , m_area(other.area())
    {
    }

    strut_rect& operator=(strut_rect const& other)
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

using strut_rects = QVector<strut_rect>;

}

Q_DECLARE_OPERATORS_FOR_FLAGS(KWin::win::StrutAreas)
