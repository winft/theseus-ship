/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>
Copyright (C) 2020 Roman Gilg <subdiff@gmail.com>

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
#include "kwin_wayland_test.h"
#include "cursor.h"
#include "platform.h"
#include "screens.h"
#include "win/win.h"
#include "workspace.h"
#include "wayland_server.h"
#include "x11client.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/surface.h>

#include <KConfigGroup>

using namespace KWin;

static const QString s_socketName = QStringLiteral("wayland_test_kwin_screens-0");

class TestScreens : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testCurrentFollowsMouse();
    void testReconfigure_data();
    void testReconfigure();
    void testSize_data();
    void testSize();
    void testCount();
    void testIntersecting_data();
    void testIntersecting();
    void testCurrent_data();
    void testCurrent();
    void testCurrentClient();
    void testCurrentWithFollowsMouse_data();
    void testCurrentWithFollowsMouse();
    void testCurrentPoint_data();
    void testCurrentPoint();

private:
    Wrapland::Client::Compositor *m_compositor = nullptr;
};

void TestScreens::initTestCase()
{
    QSignalSpy workspaceCreatedSpy(kwinApp(), &Application::workspaceCreated);
    QVERIFY(workspaceCreatedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName.toLocal8Bit()));

    kwinApp()->start();
    QVERIFY(workspaceCreatedSpy.wait());
    setenv("QT_QPA_PLATFORM", "wayland", true);
    waylandServer()->initWorkspace();
}

void TestScreens::init()
{
    Test::setupWaylandConnection();
    m_compositor = Test::waylandCompositor();

    Screens::self()->setCurrent(0);
    KWin::Cursor::setPos(QPoint(640, 512));
}

void TestScreens::cleanup()
{
    Test::destroyWaylandConnection();
}

void TestScreens::testCurrentFollowsMouse()
{
    auto screens = Screens::self();
    QVERIFY(!screens->isCurrentFollowsMouse());
    screens->setCurrentFollowsMouse(true);
    QVERIFY(screens->isCurrentFollowsMouse());
    // setting to same should not do anything
    screens->setCurrentFollowsMouse(true);
    QVERIFY(screens->isCurrentFollowsMouse());

    // setting back to other value
    screens->setCurrentFollowsMouse(false);
    QVERIFY(!screens->isCurrentFollowsMouse());
    // setting to same should not do anything
    screens->setCurrentFollowsMouse(false);
    QVERIFY(!screens->isCurrentFollowsMouse());
}

void TestScreens::testReconfigure_data()
{
    QTest::addColumn<QString>("focusPolicy");
    QTest::addColumn<bool>("expectedDefault");
    QTest::addColumn<bool>("setting");

    QTest::newRow("ClickToFocus")            << QStringLiteral("ClickToFocus")            << false << true;
    QTest::newRow("FocusFollowsMouse")       << QStringLiteral("FocusFollowsMouse")       << true  << false;
    QTest::newRow("FocusUnderMouse")         << QStringLiteral("FocusUnderMouse")         << false  << false;
    QTest::newRow("FocusStrictlyUnderMouse") << QStringLiteral("FocusStrictlyUnderMouse") << false  << false;
}

void TestScreens::testReconfigure()
{
    auto screens = Screens::self();
    screens->reconfigure();

    QTEST(screens->isCurrentFollowsMouse(), "expectedDefault");

    QFETCH(QString, focusPolicy);

    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("Windows").writeEntry("FocusPolicy", focusPolicy);
    config->group("Windows").sync();
    config->sync();

    screens->setConfig(config);
    screens->reconfigure();

    QTEST(screens->isCurrentFollowsMouse(), "expectedDefault");

    QFETCH(bool, setting);
    config->group("Windows").writeEntry("ActiveMouseScreen", setting);
    config->sync();
    screens->reconfigure();
    QCOMPARE(screens->isCurrentFollowsMouse(), setting);
}

void TestScreens::testSize_data()
{
    QTest::addColumn< QList<QRect> >("geometries");
    QTest::addColumn<QSize>("expectedSize");

    QTest::newRow("empty") << QList<QRect>{{QRect()}} << QSize(0, 0);
    QTest::newRow("cloned") << QList<QRect>{{QRect{0, 0, 200, 100}, QRect{0, 0, 200, 100}}} << QSize(200, 100);
    QTest::newRow("adjacent") << QList<QRect>{{QRect{0, 0, 200, 100}, QRect{200, 100, 400, 300}}} << QSize(600, 400);
    QTest::newRow("overlapping") << QList<QRect>{{QRect{-10, -20, 50, 100}, QRect{0, 0, 100, 200}}} << QSize(110, 220);
    QTest::newRow("gap") << QList<QRect>{{QRect{0, 0, 10, 20}, QRect{20, 40, 10, 20}}} << QSize(30, 60);
}

void TestScreens::testSize()
{
    auto screens = Screens::self();
    QSignalSpy sizeChangedSpy(screens, &KWin::Screens::sizeChanged);
    QVERIFY(sizeChangedSpy.isValid());

    QFETCH(QList<QRect>, geometries);
    QMetaObject::invokeMethod(kwinApp()->platform(),
        "setVirtualOutputs",
        Qt::DirectConnection,
        Q_ARG(int, geometries.count()),
        Q_ARG(QVector<QRect>, QVector<QRect>::fromList(geometries)),
                              Q_ARG(QVector<int>, QVector<int>(geometries.count(), 1))
    );

    QCOMPARE(sizeChangedSpy.count(), 1);
    QTEST(screens->size(), "expectedSize");
}

void TestScreens::testCount()
{
    auto screens = Screens::self();
    QSignalSpy countChangedSpy(screens, &KWin::Screens::countChanged);
    QVERIFY(countChangedSpy.isValid());

    // From previous test.
    QCOMPARE(screens->count(), 2);

    // change to two screens
    QList<QRect> geometries{{QRect{0, 0, 100, 200}, QRect{100, 0, 100, 200}}};
    QMetaObject::invokeMethod(kwinApp()->platform(),
        "setVirtualOutputs",
        Qt::DirectConnection,
        Q_ARG(int, geometries.count()),
        Q_ARG(QVector<QRect>, QVector<QRect>::fromList(geometries)),
                              Q_ARG(QVector<int>, QVector<int>(geometries.count(), 1))
    );

    QCOMPARE(countChangedSpy.count(), 0);
    QCOMPARE(screens->count(), 2);

    // go back to one screen
    geometries.takeLast();
    QMetaObject::invokeMethod(kwinApp()->platform(),
        "setVirtualOutputs",
        Qt::DirectConnection,
        Q_ARG(int, geometries.count()),
        Q_ARG(QVector<QRect>, QVector<QRect>::fromList(geometries)),
                              Q_ARG(QVector<int>, QVector<int>(geometries.count(), 1))
    );

    QCOMPARE(countChangedSpy.count(), 1);
    QCOMPARE(countChangedSpy.last().first().toInt(), 2);
    QCOMPARE(countChangedSpy.last().last().toInt(), 1);
    QCOMPARE(screens->count(), 1);

    // setting the same geometries shouldn't emit the signal, but we should get a changed signal
    QSignalSpy changedSpy(screens, &Screens::changed);
    QVERIFY(changedSpy.isValid());

    QMetaObject::invokeMethod(kwinApp()->platform(),
        "setVirtualOutputs",
        Qt::DirectConnection,
        Q_ARG(int, geometries.count()),
        Q_ARG(QVector<QRect>, QVector<QRect>::fromList(geometries)),
                              Q_ARG(QVector<int>, QVector<int>(geometries.count(), 1))
    );

    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(countChangedSpy.count(), 1);
}

void TestScreens::testIntersecting_data()
{
    QTest::addColumn<QList<QRect>>("geometries");
    QTest::addColumn<QRect>("testGeometry");
    QTest::addColumn<int>("expectedCount");

    QTest::newRow("null-rect") << QList<QRect>{{QRect{0, 0, 100, 100}}} << QRect() << 0;
    QTest::newRow("non-overlapping") << QList<QRect>{{QRect{0, 0, 100, 100}}} << QRect(100, 0, 100, 100) << 0;
    QTest::newRow("in-between") << QList<QRect>{{QRect{0, 0, 10, 20}, QRect{20, 40, 10, 20}}} << QRect(15, 0, 2, 2) << 0;
    QTest::newRow("gap-overlapping") << QList<QRect>{{QRect{0, 0, 10, 20}, QRect{20, 40, 10, 20}}} << QRect(9, 10, 200, 200) << 2;
    QTest::newRow("larger") << QList<QRect>{{QRect{0, 0, 100, 100}}} << QRect(-10, -10, 200, 200) << 1;
    QTest::newRow("several") << QList<QRect>{{QRect{0, 0, 100, 100}, QRect{100, 0, 100, 100}, QRect{200, 100, 100, 100}, QRect{300, 100, 100, 100}}} << QRect(0, 0, 300, 300) << 3;
}

void TestScreens::testIntersecting()
{
    auto screens = Screens::self();
    QSignalSpy changedSpy(screens, &KWin::Screens::changed);
    QVERIFY(changedSpy.isValid());

    QFETCH(QList<QRect>, geometries);
    QMetaObject::invokeMethod(kwinApp()->platform(),
        "setVirtualOutputs",
        Qt::DirectConnection,
        Q_ARG(int, geometries.count()),
        Q_ARG(QVector<QRect>, QVector<QRect>::fromList(geometries)),
                              Q_ARG(QVector<int>, QVector<int>(geometries.count(), 1))
    );

    QCOMPARE(changedSpy.count(), 1);

    QFETCH(QRect, testGeometry);
    QCOMPARE(screens->count(), geometries.count());
    QTEST(screens->intersecting(testGeometry), "expectedCount");
}

void TestScreens::testCurrent_data()
{
    QTest::addColumn<int>("current");
    QTest::addColumn<bool>("signal");

    QTest::newRow("unchanged") << 0 << false;
    QTest::newRow("changed") << 1 << true;
}

void TestScreens::testCurrent()
{
    auto screens = Screens::self();
    QSignalSpy currentChangedSpy(screens, &KWin::Screens::currentChanged);
    QVERIFY(currentChangedSpy.isValid());

    QFETCH(int, current);
    screens->setCurrent(current);
    QCOMPARE(screens->current(), current);
    QTEST(!currentChangedSpy.isEmpty(), "signal");
}

void TestScreens::testCurrentClient()
{
    auto screens = Screens::self();
    QSignalSpy changedSpy(screens, &KWin::Screens::changed);
    QVERIFY(changedSpy.isValid());

    QList<QRect> geometries{{QRect{0, 0, 100, 100}, QRect{100, 0, 100, 100}}};
    QMetaObject::invokeMethod(kwinApp()->platform(),
        "setVirtualOutputs",
        Qt::DirectConnection,
        Q_ARG(int, geometries.count()),
        Q_ARG(QVector<QRect>, QVector<QRect>::fromList(geometries)),
                              Q_ARG(QVector<int>, QVector<int>(geometries.count(), 1))
    );

    QCOMPARE(changedSpy.count(), 1);

    QSignalSpy currentChangedSpy(screens, &KWin::Screens::currentChanged);
    QVERIFY(currentChangedSpy.isValid());

    // Create a window.
    QSignalSpy clientAddedSpy(waylandServer(), &WaylandServer::shellClientAdded);
    QVERIFY(clientAddedSpy.isValid());
    auto surface = Test::createSurface(m_compositor);
    QVERIFY(surface);
    auto shellSurface = Test::createXdgShellStableSurface(surface, surface);
    QVERIFY(shellSurface);
    Test::render(surface, QSize(100, 50), Qt::blue);
    Test::flushWaylandConnection();
    QVERIFY(clientAddedSpy.wait());
    auto client = workspace()->activeClient();
    QVERIFY(client);

    client->move(101, 0);
    QCOMPARE(Workspace::self()->activeClient(), client);
    Workspace::self()->setActiveClient(nullptr);
    QCOMPARE(Workspace::self()->activeClient(), nullptr);

    // it's not the active client, so changing won't work
    screens->setCurrent(client);
    QVERIFY(currentChangedSpy.isEmpty());
    QCOMPARE(screens->current(), 0);

    // making the client active should affect things
    win::set_active(client, true);
    Workspace::self()->setActiveClient(client);

    // first of all current should be changed just by the fact that there is an active client
    QCOMPARE(screens->current(), 1);
    // but also calling setCurrent should emit the changed signal
    screens->setCurrent(client);
    QCOMPARE(currentChangedSpy.count(), 1);
    QCOMPARE(screens->current(), 1);

    // setting current with the same client again should not change, though
    screens->setCurrent(client);
    QCOMPARE(currentChangedSpy.count(), 1);

    // and it should even still be on screen 1 if we make the client non-current again
    Workspace::self()->setActiveClient(nullptr);
    win::set_active(client, false);
    QCOMPARE(screens->current(), 1);
}

void TestScreens::testCurrentWithFollowsMouse_data()
{
    QTest::addColumn< QList<QRect> >("geometries");
    QTest::addColumn<QPoint>("cursorPos");
    QTest::addColumn<int>("expected");

    QTest::newRow("empty") << QList<QRect>{{QRect()}} << QPoint(100, 100) << 0;
    QTest::newRow("cloned") << QList<QRect>{{QRect{0, 0, 200, 100}, QRect{0, 0, 200, 100}}} << QPoint(50, 50) << 0;
    QTest::newRow("adjacent-0") << QList<QRect>{{QRect{0, 0, 200, 100}, QRect{200, 100, 400, 300}}} << QPoint(199, 99) << 0;
    QTest::newRow("adjacent-1") << QList<QRect>{{QRect{0, 0, 200, 100}, QRect{200, 100, 400, 300}}} << QPoint(200, 100) << 1;
    QTest::newRow("gap") << QList<QRect>{{QRect{0, 0, 10, 20}, QRect{20, 40, 10, 20}}} << QPoint(15, 30) << 0;
}

void TestScreens::testCurrentWithFollowsMouse()
{
    auto screens = Screens::self();
    QSignalSpy changedSpy(screens, &KWin::Screens::changed);
    QVERIFY(changedSpy.isValid());
    screens->setCurrentFollowsMouse(true);
    kwinApp()->platform()->pointerMotion(QPointF(0, 0), 1);
    QCOMPARE(screens->current(), 0);

    QFETCH(QList<QRect>, geometries);
    QMetaObject::invokeMethod(kwinApp()->platform(),
        "setVirtualOutputs",
        Qt::DirectConnection,
        Q_ARG(int, geometries.count()),
        Q_ARG(QVector<QRect>, QVector<QRect>::fromList(geometries)),
                              Q_ARG(QVector<int>, QVector<int>(geometries.count(), 1))
    );

    QCOMPARE(changedSpy.count(), 1);

    QFETCH(QPoint, cursorPos);
    kwinApp()->platform()->pointerMotion(cursorPos, 2);
//    KWin::s_cursorPos = cursorPos;
    QTEST(screens->current(), "expected");
}

void TestScreens::testCurrentPoint_data()
{
    QTest::addColumn< QList<QRect> >("geometries");
    QTest::addColumn<QPoint>("cursorPos");
    QTest::addColumn<int>("expected");

    QTest::newRow("empty") << QList<QRect>{{QRect()}} << QPoint(100, 100) << 0;
    QTest::newRow("cloned") << QList<QRect>{{QRect{0, 0, 200, 100}, QRect{0, 0, 200, 100}}} << QPoint(50, 50) << 0;
    QTest::newRow("adjacent-0") << QList<QRect>{{QRect{0, 0, 200, 100}, QRect{200, 100, 400, 300}}} << QPoint(199, 99) << 0;
    QTest::newRow("adjacent-1") << QList<QRect>{{QRect{0, 0, 200, 100}, QRect{200, 100, 400, 300}}} << QPoint(200, 100) << 1;
    QTest::newRow("gap") << QList<QRect>{{QRect{0, 0, 10, 20}, QRect{20, 40, 10, 20}}} << QPoint(15, 30) << 1;
}

void TestScreens::testCurrentPoint()
{
    auto screens = Screens::self();
    QSignalSpy changedSpy(screens, &KWin::Screens::changed);
    QVERIFY(changedSpy.isValid());

    QFETCH(QList<QRect>, geometries);
    QMetaObject::invokeMethod(kwinApp()->platform(),
        "setVirtualOutputs",
        Qt::DirectConnection,
        Q_ARG(int, geometries.count()),
        Q_ARG(QVector<QRect>, QVector<QRect>::fromList(geometries)),
                              Q_ARG(QVector<int>, QVector<int>(geometries.count(), 1))
    );

    QCOMPARE(changedSpy.count(), 1);

    QFETCH(QPoint, cursorPos);
    screens->setCurrent(cursorPos);
    QTEST(screens->current(), "expected");
}

WAYLANDTEST_MAIN(TestScreens)
#include "screens.moc"
