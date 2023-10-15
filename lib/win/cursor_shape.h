/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"
#include <QObject>

namespace KWin::win
{

namespace extended_cursor
{
/**
 * Extension of Qt::CursorShape with values not currently present there
 */
enum Shape {
    SizeNorthWest = 0x100 + 0,
    SizeNorth = 0x100 + 1,
    SizeNorthEast = 0x100 + 2,
    SizeEast = 0x100 + 3,
    SizeWest = 0x100 + 4,
    SizeSouthEast = 0x100 + 5,
    SizeSouth = 0x100 + 6,
    SizeSouthWest = 0x100 + 7
};
}

/**
 * @brief Wrapper round Qt::CursorShape with extensions enums into a single entity
 */
class KWIN_EXPORT cursor_shape
{
public:
    cursor_shape() = default;
    cursor_shape(Qt::CursorShape qtShape);
    cursor_shape(extended_cursor::Shape kwinShape);

    bool operator==(cursor_shape const& o) const;
    operator int() const;

    /**
     * @brief The name of a cursor shape in the theme.
     */
    std::string name() const;

private:
    int m_shape{Qt::ArrowCursor};
};

}

Q_DECLARE_METATYPE(KWin::win::cursor_shape)
