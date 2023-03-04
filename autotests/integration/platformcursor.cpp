/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"

namespace KWin::detail::test
{

TEST_CASE("platform cursor", "[input]")
{
    // this test verifies that the PlatformCursor of the QPA plugin forwards ::pos and ::setPos
    // correctly that is QCursor should work just like KWin::Cursor

    test::setup setup("platform-cursor");
    setup.start();

    // cursor should be centered on screen
    QCOMPARE(cursor()->pos(), QPoint(639, 511));
    QCOMPARE(QCursor::pos(), QPoint(639, 511));

    // let's set the pos through QCursor API
    QCursor::setPos(QPoint(10, 10));
    QCOMPARE(cursor()->pos(), QPoint(10, 10));
    QCOMPARE(QCursor::pos(), QPoint(10, 10));

    // and let's set the pos through Cursor API
    QCursor::setPos(QPoint(20, 20));
    QCOMPARE(cursor()->pos(), QPoint(20, 20));
    QCOMPARE(QCursor::pos(), QPoint(20, 20));
}

}
