/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/app.h"

#include "base/wayland/server.h"
#include "input/cursor.h"

namespace KWin
{

class PlatformCursorTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void testPos();
};

void PlatformCursorTest::initTestCase()
{
    QSignalSpy startup_spy(Test::app(), &WaylandTestApplication::startup_finished);
    QVERIFY(startup_spy.isValid());
    Test::app()->start();
    QVERIFY(startup_spy.wait());
}

void PlatformCursorTest::testPos()
{
    // this test verifies that the PlatformCursor of the QPA plugin forwards ::pos and ::setPos
    // correctly that is QCursor should work just like KWin::Cursor

    // cursor should be centered on screen
    QCOMPARE(Test::cursor()->pos(), QPoint(639, 511));
    QCOMPARE(QCursor::pos(), QPoint(639, 511));

    // let's set the pos through QCursor API
    QCursor::setPos(QPoint(10, 10));
    QCOMPARE(Test::cursor()->pos(), QPoint(10, 10));
    QCOMPARE(QCursor::pos(), QPoint(10, 10));

    // and let's set the pos through Cursor API
    QCursor::setPos(QPoint(20, 20));
    QCOMPARE(Test::cursor()->pos(), QPoint(20, 20));
    QCOMPARE(QCursor::pos(), QPoint(20, 20));
}

}

WAYLANDTEST_MAIN(KWin::PlatformCursorTest)
#include "platformcursor.moc"
