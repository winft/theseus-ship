/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>

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
    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());
    Test::app()->start();
    QVERIFY(startup_spy.wait());
}

void PlatformCursorTest::testPos()
{
    // this test verifies that the PlatformCursor of the QPA plugin forwards ::pos and ::setPos
    // correctly that is QCursor should work just like KWin::Cursor

    // cursor should be centered on screen
    QCOMPARE(Test::app()->input->cursor->pos(), QPoint(639, 511));
    QCOMPARE(QCursor::pos(), QPoint(639, 511));

    // let's set the pos through QCursor API
    QCursor::setPos(QPoint(10, 10));
    QCOMPARE(Test::app()->input->cursor->pos(), QPoint(10, 10));
    QCOMPARE(QCursor::pos(), QPoint(10, 10));

    // and let's set the pos through Cursor API
    QCursor::setPos(QPoint(20, 20));
    QCOMPARE(Test::app()->input->cursor->pos(), QPoint(20, 20));
    QCOMPARE(QCursor::pos(), QPoint(20, 20));
}

}

WAYLANDTEST_MAIN(KWin::PlatformCursorTest)
#include "platformcursor.moc"
