/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platformcursor.h"

#include "input/cursor.h"
#include "input/platform.h"
#include "input/singleton_interface.h"

namespace KWin
{
namespace QPA
{

PlatformCursor::PlatformCursor()
    : QPlatformCursor()
{
}

PlatformCursor::~PlatformCursor() = default;

QPoint PlatformCursor::pos() const
{
    return input::singleton_interface::cursor->pos();
}

void PlatformCursor::setPos(const QPoint& pos)
{
    input::singleton_interface::cursor->set_pos(pos);
}

void PlatformCursor::changeCursor(QCursor* windowCursor, QWindow* window)
{
    Q_UNUSED(windowCursor)
    Q_UNUSED(window)
    // TODO: implement
}

}
}
