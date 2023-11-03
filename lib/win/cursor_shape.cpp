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

std::string cursor_shape::name() const
{
    switch (m_shape) {
    case Qt::ArrowCursor:
        return "left_ptr";
    case Qt::UpArrowCursor:
        return "up_arrow";
    case Qt::CrossCursor:
        return "cross";
    case Qt::WaitCursor:
        return "wait";
    case Qt::IBeamCursor:
        return "ibeam";
    case Qt::SizeVerCursor:
        return "size_ver";
    case Qt::SizeHorCursor:
        return "size_hor";
    case Qt::SizeBDiagCursor:
        return "size_bdiag";
    case Qt::SizeFDiagCursor:
        return "size_fdiag";
    case Qt::SizeAllCursor:
        return "size_all";
    case Qt::SplitVCursor:
        return "split_v";
    case Qt::SplitHCursor:
        return "split_h";
    case Qt::PointingHandCursor:
        return "pointing_hand";
    case Qt::ForbiddenCursor:
        return "forbidden";
    case Qt::OpenHandCursor:
        return "openhand";
    case Qt::ClosedHandCursor:
        return "closedhand";
    case Qt::WhatsThisCursor:
        return "whats_this";
    case Qt::BusyCursor:
        return "left_ptr_watch";
    case Qt::DragMoveCursor:
        return "dnd-move";
    case Qt::DragCopyCursor:
        return "dnd-copy";
    case Qt::DragLinkCursor:
        return "dnd-link";
    case extended_cursor::SizeNorthEast:
        return "ne-resize";
    case extended_cursor::SizeNorth:
        return "n-resize";
    case extended_cursor::SizeNorthWest:
        return "nw-resize";
    case extended_cursor::SizeEast:
        return "e-resize";
    case extended_cursor::SizeWest:
        return "w-resize";
    case extended_cursor::SizeSouthEast:
        return "se-resize";
    case extended_cursor::SizeSouth:
        return "s-resize";
    case extended_cursor::SizeSouthWest:
        return "sw-resize";
    default:
        return "";
    }
}

}
