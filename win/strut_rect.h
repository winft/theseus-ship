/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

#include <QRect>
#include <vector>

namespace KWin::win
{

class strut_rect : public QRect
{
public:
    explicit strut_rect(QRect rect = QRect(), strut_area area = strut_area::invalid)
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

    inline strut_area area() const
    {
        return m_area;
    }

private:
    strut_area m_area;
};

using strut_rects = std::vector<strut_rect>;

}
