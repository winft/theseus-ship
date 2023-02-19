/*
SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/app.h"

#include "base/wayland/server.h"
#include "win/deco.h"
#include "win/space.h"
#include "win/x11/window.h"

#include <KDecoration2/Decoration>

namespace KWin
{

class DontCrashGlxgearsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void testGlxgears();
};

void DontCrashGlxgearsTest::initTestCase()
{
    QSignalSpy startup_spy(Test::app(), &WaylandTestApplication::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    QVERIFY(startup_spy.wait());
}

void DontCrashGlxgearsTest::testGlxgears()
{
    // closing a glxgears window through Aurorae themes used to crash KWin
    // Let's make sure that doesn't happen anymore

    QSignalSpy clientAddedSpy(Test::app()->base->space->qobject.get(),
                              &win::space::qobject_t::clientAdded);
    QVERIFY(clientAddedSpy.isValid());

    QProcess glxgears;
    glxgears.setProgram(QStringLiteral("glxgears"));
    glxgears.start();
    QVERIFY(glxgears.waitForStarted());

    QVERIFY(clientAddedSpy.wait());
    QCOMPARE(clientAddedSpy.count(), 1);
    QCOMPARE(Test::app()->base->space->windows.size(), 1);

    auto glxgearsClient = Test::get_x11_window(Test::app()->base->space->windows.front());
    QVERIFY(glxgearsClient);
    QVERIFY(win::decoration(glxgearsClient));
    QSignalSpy closedSpy(glxgearsClient->qobject.get(), &win::window_qobject::closed);
    QVERIFY(closedSpy.isValid());

    auto decoration = win::decoration(glxgearsClient);
    QVERIFY(decoration);

    // send a mouse event to the position of the close button
    // TODO: position is dependent on the decoration in use. We should use a static target instead,
    // a fake deco for autotests.
    QPointF pos = decoration->rect().topRight()
        + QPointF(-decoration->borderTop() / 2, decoration->borderTop() / 2);
    QHoverEvent event(QEvent::HoverMove, pos, pos);
    QCoreApplication::instance()->sendEvent(decoration, &event);
    // mouse press
    QMouseEvent mousePressevent(
        QEvent::MouseButtonPress, pos, pos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    mousePressevent.setAccepted(false);
    QCoreApplication::sendEvent(decoration, &mousePressevent);
    QVERIFY(mousePressevent.isAccepted());
    // mouse Release
    QMouseEvent mouseReleaseEvent(
        QEvent::MouseButtonRelease, pos, pos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    mouseReleaseEvent.setAccepted(false);
    QCoreApplication::sendEvent(decoration, &mouseReleaseEvent);
    QVERIFY(mouseReleaseEvent.isAccepted());

    QVERIFY(closedSpy.wait());
    QCOMPARE(closedSpy.count(), 1);
    xcb_flush(Test::app()->base->x11_data.connection);

    if (glxgears.state() == QProcess::Running) {
        QVERIFY(glxgears.waitForFinished());
    }
}

}

WAYLANDTEST_MAIN(KWin::DontCrashGlxgearsTest)

#include "dont_crash_glxgears.moc"
