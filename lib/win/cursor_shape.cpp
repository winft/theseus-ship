/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "cursor_shape.h"

namespace KWin::win
{

cursor_shape::cursor_shape(Qt::CursorShape qtShape)
    : m_shape{qtShape}
{
}

cursor_shape::cursor_shape(extended_cursor::Shape kwinShape)
    : m_shape{kwinShape}
{
}

bool cursor_shape::operator==(cursor_shape const& o) const
{
    return m_shape == o.m_shape;
}

cursor_shape::operator int() const
{
    return m_shape;
}

QByteArray cursor_shape::name() const
{
    switch (m_shape) {
    case Qt::ArrowCursor:
        return QByteArrayLiteral("left_ptr");
    case Qt::UpArrowCursor:
        return QByteArrayLiteral("up_arrow");
    case Qt::CrossCursor:
        return QByteArrayLiteral("cross");
    case Qt::WaitCursor:
        return QByteArrayLiteral("wait");
    case Qt::IBeamCursor:
        return QByteArrayLiteral("ibeam");
    case Qt::SizeVerCursor:
        return QByteArrayLiteral("size_ver");
    case Qt::SizeHorCursor:
        return QByteArrayLiteral("size_hor");
    case Qt::SizeBDiagCursor:
        return QByteArrayLiteral("size_bdiag");
    case Qt::SizeFDiagCursor:
        return QByteArrayLiteral("size_fdiag");
    case Qt::SizeAllCursor:
        return QByteArrayLiteral("size_all");
    case Qt::SplitVCursor:
        return QByteArrayLiteral("split_v");
    case Qt::SplitHCursor:
        return QByteArrayLiteral("split_h");
    case Qt::PointingHandCursor:
        return QByteArrayLiteral("pointing_hand");
    case Qt::ForbiddenCursor:
        return QByteArrayLiteral("forbidden");
    case Qt::OpenHandCursor:
        return QByteArrayLiteral("openhand");
    case Qt::ClosedHandCursor:
        return QByteArrayLiteral("closedhand");
    case Qt::WhatsThisCursor:
        return QByteArrayLiteral("whats_this");
    case Qt::BusyCursor:
        return QByteArrayLiteral("left_ptr_watch");
    case Qt::DragMoveCursor:
        return QByteArrayLiteral("dnd-move");
    case Qt::DragCopyCursor:
        return QByteArrayLiteral("dnd-copy");
    case Qt::DragLinkCursor:
        return QByteArrayLiteral("dnd-link");
    case extended_cursor::SizeNorthEast:
        return QByteArrayLiteral("ne-resize");
    case extended_cursor::SizeNorth:
        return QByteArrayLiteral("n-resize");
    case extended_cursor::SizeNorthWest:
        return QByteArrayLiteral("nw-resize");
    case extended_cursor::SizeEast:
        return QByteArrayLiteral("e-resize");
    case extended_cursor::SizeWest:
        return QByteArrayLiteral("w-resize");
    case extended_cursor::SizeSouthEast:
        return QByteArrayLiteral("se-resize");
    case extended_cursor::SizeSouth:
        return QByteArrayLiteral("s-resize");
    case extended_cursor::SizeSouthWest:
        return QByteArrayLiteral("sw-resize");
    default:
        return QByteArray();
    }
}

}
