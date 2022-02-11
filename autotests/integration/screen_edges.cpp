/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2014 Martin Gräßlin <mgraesslin@kde.org>
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
#include "lib/app.h"

#include "base/wayland/server.h"
#include "base/x11/xcb/proto.h"
#include "input/cursor.h"
#include "input/gestures.h"
#include "screens.h"
#include "toplevel.h"
#include "win/screen_edges.h"
#include "win/stacking.h"
#include "win/wayland/space.h"
#include "win/wayland/window.h"
#include "workspace.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/surface.h>

#include <KConfigGroup>

#include <QDateTime>

Q_DECLARE_METATYPE(KWin::ElectricBorder)

namespace KWin
{

class TestScreenEdges : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testInit();
    void testCreatingInitialEdges();
    void testCallback();
    void testCallbackWithCheck();
    void test_overlapping_edges_data();
    void test_overlapping_edges();
    void testPushBack_data();
    void testPushBack();
    void testFullScreenBlocking();
    void testClientEdge();
    void testTouchEdge();
    void testTouchCallback_data();
    void testTouchCallback();

private:
    Wrapland::Client::Compositor* m_compositor = nullptr;
};

void TestScreenEdges::initTestCase()
{
    qRegisterMetaType<KWin::ElectricBorder>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    QVERIFY(startup_spy.wait());
}

void TestScreenEdges::init()
{
    Test::setup_wayland_connection();
    m_compositor = Test::get_client().interfaces.compositor.get();

    Test::app()->base.screens.setCurrent(0);
    input::get_cursor()->set_pos(QPoint(640, 512));
}

void TestScreenEdges::cleanup()
{
    Test::destroy_wayland_connection();
}

class TestObject : public QObject
{
    Q_OBJECT
public Q_SLOTS:
    bool callback(ElectricBorder border);
Q_SIGNALS:
    void gotCallback(KWin::ElectricBorder);
};

bool TestObject::callback(KWin::ElectricBorder border)
{
    qDebug() << "GOT CALLBACK" << border;
    Q_EMIT gotCallback(border);
    return true;
}

void reset_edger()
{
    workspace()->edges = std::make_unique<win::screen_edger>(*Test::app()->workspace);
}

void reset_edger(KSharedConfig::Ptr config)
{
    kwinApp()->setConfig(config);
    reset_edger();
}

void TestScreenEdges::testInit()
{
    reset_edger();
    auto& screenEdges = workspace()->edges;
    QCOMPARE(screenEdges->desktop_switching.always, false);
    QCOMPARE(screenEdges->desktop_switching.when_moving_client, false);
    QCOMPARE(screenEdges->time_threshold, 150);
    QCOMPARE(screenEdges->reactivate_threshold, 350);
    QCOMPARE(screenEdges->cursor_push_back_distance, QSize(1, 1));
    QCOMPARE(screenEdges->actions.top_left, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.top, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.top_right, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.right, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.bottom_right, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.bottom, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.bottom_left, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.left, ElectricBorderAction::ElectricActionNone);

    auto edges
        = screenEdges->findChildren<win::screen_edge*>(QString(), Qt::FindDirectChildrenOnly);
    QCOMPARE(edges.size(), 8);
    for (auto e : edges) {
        //        QVERIFY(e->isReserved());
        QVERIFY(e->inherits("QObject"));
        QVERIFY(!e->client());
        QVERIFY(!e->is_approaching);
    }
    auto te = edges.at(0);
    QVERIFY(te->isCorner());
    QVERIFY(!te->isScreenEdge());
    QVERIFY(te->isLeft());
    QVERIFY(te->isTop());
    QVERIFY(!te->isRight());
    QVERIFY(!te->isBottom());
    QCOMPARE(te->border, ElectricBorder::ElectricTopLeft);
    te = edges.at(1);
    QVERIFY(te->isCorner());
    QVERIFY(!te->isScreenEdge());
    QVERIFY(te->isLeft());
    QVERIFY(!te->isTop());
    QVERIFY(!te->isRight());
    QVERIFY(te->isBottom());
    QCOMPARE(te->border, ElectricBorder::ElectricBottomLeft);
    te = edges.at(2);
    QVERIFY(!te->isCorner());
    QVERIFY(te->isScreenEdge());
    QVERIFY(te->isLeft());
    QVERIFY(!te->isTop());
    QVERIFY(!te->isRight());
    QVERIFY(!te->isBottom());
    QCOMPARE(te->border, ElectricBorder::ElectricLeft);
    te = edges.at(3);
    QVERIFY(te->isCorner());
    QVERIFY(!te->isScreenEdge());
    QVERIFY(!te->isLeft());
    QVERIFY(te->isTop());
    QVERIFY(te->isRight());
    QVERIFY(!te->isBottom());
    QCOMPARE(te->border, ElectricBorder::ElectricTopRight);
    te = edges.at(4);
    QVERIFY(te->isCorner());
    QVERIFY(!te->isScreenEdge());
    QVERIFY(!te->isLeft());
    QVERIFY(!te->isTop());
    QVERIFY(te->isRight());
    QVERIFY(te->isBottom());
    QCOMPARE(te->border, ElectricBorder::ElectricBottomRight);
    te = edges.at(5);
    QVERIFY(!te->isCorner());
    QVERIFY(te->isScreenEdge());
    QVERIFY(!te->isLeft());
    QVERIFY(!te->isTop());
    QVERIFY(te->isRight());
    QVERIFY(!te->isBottom());
    QCOMPARE(te->border, ElectricBorder::ElectricRight);
    te = edges.at(6);
    QVERIFY(!te->isCorner());
    QVERIFY(te->isScreenEdge());
    QVERIFY(!te->isLeft());
    QVERIFY(te->isTop());
    QVERIFY(!te->isRight());
    QVERIFY(!te->isBottom());
    QCOMPARE(te->border, ElectricBorder::ElectricTop);
    te = edges.at(7);
    QVERIFY(!te->isCorner());
    QVERIFY(te->isScreenEdge());
    QVERIFY(!te->isLeft());
    QVERIFY(!te->isTop());
    QVERIFY(!te->isRight());
    QVERIFY(te->isBottom());
    QCOMPARE(te->border, ElectricBorder::ElectricBottom);

    // we shouldn't have any x windows, though
    QCOMPARE(screenEdges->windows().size(), 0);
}

void TestScreenEdges::testCreatingInitialEdges()
{
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("Windows").writeEntry("ElectricBorders", 2 /*ElectricAlways*/);
    config->sync();

    reset_edger(config);
    auto& screenEdges = workspace()->edges;

    // we don't have multiple desktops, so it's returning false
    QCOMPARE(screenEdges->desktop_switching.always, true);
    QCOMPARE(screenEdges->desktop_switching.when_moving_client, true);
    QCOMPARE(screenEdges->actions.top_left, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.top, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.top_right, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.right, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.bottom_right, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.bottom, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.bottom_left, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.left, ElectricBorderAction::ElectricActionNone);

    QCOMPARE(screenEdges->windows().size(), 0);

    // set some reasonable virtual desktops
    config->group("Desktops").writeEntry("Number", 4);
    config->sync();
    auto vd = win::virtual_desktop_manager::self();
    vd->setConfig(config);
    vd->load();
    vd->updateLayout();
    QCOMPARE(vd->count(), 4u);
    QCOMPARE(vd->grid().width(), 4);
    QCOMPARE(vd->grid().height(), 1);

    // approach windows for edges not created as screen too small
    screenEdges->updateLayout();
    auto edgeWindows = screenEdges->windows();

    QEXPECT_FAIL("", "No window edges on Wayland. Needs investigation.", Abort);
    QCOMPARE(edgeWindows.size(), 12);

    auto testWindowGeometry = [&](int index) {
        base::x11::xcb::geometry geo(edgeWindows[index]);
        return geo.rect();
    };
    QRect sg = Test::app()->base.screens.geometry();
    auto const co = screenEdges->corner_offset;
    QList<QRect> expectedGeometries{
        QRect(0, 0, 1, 1),
        QRect(0, 0, co, co),
        QRect(0, sg.bottom(), 1, 1),
        QRect(0, sg.height() - co, co, co),
        QRect(0, co, 1, sg.height() - co * 2),
        //         QRect(0, co * 2 + 1, co, sg.height() - co*4),
        QRect(sg.right(), 0, 1, 1),
        QRect(sg.right() - co + 1, 0, co, co),
        QRect(sg.right(), sg.bottom(), 1, 1),
        QRect(sg.right() - co + 1, sg.bottom() - co + 1, co, co),
        QRect(sg.right(), co, 1, sg.height() - co * 2),
        //         QRect(sg.right() - co + 1, co * 2, co, sg.height() - co*4),
        QRect(co, 0, sg.width() - co * 2, 1),
        //         QRect(co * 2, 0, sg.width() - co * 4, co),
        QRect(co, sg.bottom(), sg.width() - co * 2, 1),
        //         QRect(co * 2, sg.height() - co, sg.width() - co * 4, co)
    };
    for (int i = 0; i < 12; ++i) {
        QCOMPARE(testWindowGeometry(i), expectedGeometries.at(i));
    }
    auto edges
        = screenEdges->findChildren<win::screen_edge*>(QString(), Qt::FindDirectChildrenOnly);
    QCOMPARE(edges.size(), 8);
    for (auto e : edges) {
        QVERIFY(e->reserved_count > 0);
        QCOMPARE(e->activatesForPointer(), true);
        QCOMPARE(e->activatesForTouchGesture(), false);
    }

    QSignalSpy changedSpy(&Test::app()->base.screens, &Screens::changed);
    QVERIFY(changedSpy.isValid());

    Test::app()->set_outputs({{0, 0, 1024, 768}});
    QCOMPARE(changedSpy.count(), 1);

    // let's update the layout and verify that we have edges
    screenEdges->recreateEdges();
    edgeWindows = screenEdges->windows();
    QCOMPARE(edgeWindows.size(), 16);
    sg = Test::app()->base.screens.geometry();
    expectedGeometries = QList<QRect>{QRect(0, 0, 1, 1),
                                      QRect(0, 0, co, co),
                                      QRect(0, sg.bottom(), 1, 1),
                                      QRect(0, sg.height() - co, co, co),
                                      QRect(0, co, 1, sg.height() - co * 2),
                                      QRect(0, co * 2 + 1, co, sg.height() - co * 4),
                                      QRect(sg.right(), 0, 1, 1),
                                      QRect(sg.right() - co + 1, 0, co, co),
                                      QRect(sg.right(), sg.bottom(), 1, 1),
                                      QRect(sg.right() - co + 1, sg.bottom() - co + 1, co, co),
                                      QRect(sg.right(), co, 1, sg.height() - co * 2),
                                      QRect(sg.right() - co + 1, co * 2, co, sg.height() - co * 4),
                                      QRect(co, 0, sg.width() - co * 2, 1),
                                      QRect(co * 2, 0, sg.width() - co * 4, co),
                                      QRect(co, sg.bottom(), sg.width() - co * 2, 1),
                                      QRect(co * 2, sg.height() - co, sg.width() - co * 4, co)};
    for (int i = 0; i < 16; ++i) {
        QCOMPARE(testWindowGeometry(i), expectedGeometries.at(i));
    }

    // disable desktop switching again
    config->group("Windows").writeEntry("ElectricBorders", 1 /*ElectricMoveOnly*/);
    screenEdges->reconfigure();
    QCOMPARE(screenEdges->desktop_switching.always, false);
    QCOMPARE(screenEdges->desktop_switching.when_moving_client, true);
    QCOMPARE(screenEdges->windows().size(), 0);
    edges = screenEdges->findChildren<win::screen_edge*>(QString(), Qt::FindDirectChildrenOnly);
    QCOMPARE(edges.size(), 8);
    for (int i = 0; i < 8; ++i) {
        auto e = edges.at(i);
        QVERIFY(e->reserved_count == 0);
        QCOMPARE(e->activatesForPointer(), false);
        QCOMPARE(e->activatesForTouchGesture(), false);
        QCOMPARE(e->approach_geometry, expectedGeometries.at(i * 2 + 1));
    }

    // Let's start a window move. First create a window.
    QSignalSpy clientAddedSpy(static_cast<win::wayland::space*>(workspace()),
                              &win::wayland::space::wayland_window_added);
    QVERIFY(clientAddedSpy.isValid());
    auto surface = Test::create_surface();
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);
    Test::render(surface, QSize(100, 50), Qt::blue);
    Test::flush_wayland_connection();
    QVERIFY(clientAddedSpy.wait());
    auto client = workspace()->activeClient();
    QVERIFY(client);

    workspace()->setMoveResizeClient(client);
    for (int i = 0; i < 8; ++i) {
        auto e = edges.at(i);
        QVERIFY(e->reserved_count > 0);
        QCOMPARE(e->activatesForPointer(), true);
        QCOMPARE(e->activatesForTouchGesture(), false);
        QCOMPARE(e->approach_geometry, expectedGeometries.at(i * 2 + 1));
    }
    // not for resize
    //    win::start_move_resize(client);
    //    client->setResize(true);
    for (int i = 0; i < 8; ++i) {
        auto e = edges.at(i);
        QVERIFY(e->reserved_count > 0);
        QCOMPARE(e->activatesForPointer(), false);
        QCOMPARE(e->activatesForTouchGesture(), false);
        QCOMPARE(e->approach_geometry, expectedGeometries.at(i * 2 + 1));
    }
    workspace()->setMoveResizeClient(nullptr);
}

void TestScreenEdges::testCallback()
{
    QSignalSpy changedSpy(&Test::app()->base.screens, &Screens::changed);
    QVERIFY(changedSpy.isValid());

    auto const geometries = std::vector<QRect>{{0, 0, 1024, 768}, {200, 768, 1024, 768}};
    Test::app()->set_outputs(geometries);

    QCOMPARE(changedSpy.count(), geometries.size() + 2);

    reset_edger();
    auto& screenEdges = workspace()->edges;
    TestObject callback;
    QSignalSpy spy(&callback, &TestObject::gotCallback);
    QVERIFY(spy.isValid());
    screenEdges->reserve(ElectricLeft, &callback, "callback");
    screenEdges->reserve(ElectricTopLeft, &callback, "callback");
    screenEdges->reserve(ElectricTop, &callback, "callback");
    screenEdges->reserve(ElectricTopRight, &callback, "callback");
    screenEdges->reserve(ElectricRight, &callback, "callback");
    screenEdges->reserve(ElectricBottomRight, &callback, "callback");
    screenEdges->reserve(ElectricBottom, &callback, "callback");
    screenEdges->reserve(ElectricBottomLeft, &callback, "callback");

    auto edges
        = screenEdges->findChildren<win::screen_edge*>(QString(), Qt::FindDirectChildrenOnly);
    QCOMPARE(edges.size(), 10);
    for (auto e : edges) {
        QVERIFY(e->reserved_count > 0);
        QCOMPARE(e->activatesForPointer(), true);
        //        QCOMPARE(e->activatesForTouchGesture(), true);
    }
    auto it = std::find_if(edges.constBegin(), edges.constEnd(), [](auto e) {
        return e->isScreenEdge() && e->isLeft() && e->approach_geometry.bottom() < 768;
    });
    QVERIFY(it != edges.constEnd());

    int time = 0;
    auto setPos = [&time](const QPoint& pos) {
        Test::pointer_motion_absolute(pos, QDateTime::currentMSecsSinceEpoch());
    };

    setPos(QPoint(0, 50));

    // doesn't trigger as the edge was not triggered yet
    QVERIFY(spy.isEmpty());
    QCOMPARE(input::get_cursor()->pos(), QPoint(1, 50));

    // test doesn't trigger due to too much offset
    QTest::qWait(160);
    setPos(QPoint(0, 100));
    QVERIFY(spy.isEmpty());
    QCOMPARE(input::get_cursor()->pos(), QPoint(1, 100));

    // doesn't trigger as we are waiting too long already
    QTest::qWait(200);
    setPos(QPoint(0, 101));

    QVERIFY(spy.isEmpty());
    QCOMPARE(input::get_cursor()->pos(), QPoint(1, 101));

    spy.clear();

    // doesn't activate as we are waiting too short
    QTest::qWait(50);
    setPos(QPoint(0, 100));
    QVERIFY(spy.isEmpty());
    QCOMPARE(input::get_cursor()->pos(), QPoint(1, 100));

    // and this one triggers
    QTest::qWait(110);
    setPos(QPoint(0, 101));
    QEXPECT_FAIL("",
                 "Is the other way around on Wayland than it was on X11. Needs investigation.",
                 Continue);
    QVERIFY(!spy.isEmpty());

    QEXPECT_FAIL("", "No dead pixel on Wayland? Needs investigation.", Continue);
    QCOMPARE(input::get_cursor()->pos(), QPoint(1, 101));

    // now let's try to trigger again
    QTest::qWait(351);
    setPos(QPoint(0, 100));

    QEXPECT_FAIL("",
                 "Is the other way around on Wayland than it was on X11. Needs investigation.",
                 Continue);
    QCOMPARE(spy.count(), 1);

    QEXPECT_FAIL("", "No pushback on Wayland. Needs investigation.", Continue);
    QCOMPARE(input::get_cursor()->pos(), QPoint(1, 100));

    // it's still under the reactivation
    QTest::qWait(50);
    setPos(QPoint(0, 100));

    QEXPECT_FAIL("",
                 "Is the other way around on Wayland than it was on X11. Needs investigation.",
                 Continue);
    QCOMPARE(spy.count(), 1);

    QEXPECT_FAIL("", "No pushback on Wayland. Needs investigation.", Continue);
    QCOMPARE(input::get_cursor()->pos(), QPoint(1, 100));

    // now it should trigger again
    QTest::qWait(250);
    setPos(QPoint(0, 100));

    QEXPECT_FAIL(
        "", "Is the other way around on Wayland than it was on X11. Needs investigation.", Abort);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.first().first().value<ElectricBorder>(), ElectricLeft);
    QCOMPARE(spy.last().first().value<ElectricBorder>(), ElectricLeft);
    QCOMPARE(input::get_cursor()->pos(), QPoint(1, 100));

    // let's disable pushback
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("Windows").writeEntry("ElectricBorderPushbackPixels", 0);
    config->sync();
    screenEdges->config = config;
    screenEdges->reconfigure();

    // it should trigger directly
    QTest::qWait(350);
    QEXPECT_FAIL("",
                 "Is the other way around on Wayland than it was on X11. Needs investigation.",
                 Continue);
    QCOMPARE(spy.count(), 3);
    QCOMPARE(spy.at(0).first().value<ElectricBorder>(), ElectricLeft);
#if 0
    QCOMPARE(spy.at(1).first().value<ElectricBorder>(), ElectricLeft);
    QCOMPARE(spy.at(2).first().value<ElectricBorder>(), ElectricLeft);
#endif
    QEXPECT_FAIL("", "No dead pixel on Wayland? Needs investigation.", Continue);
    QCOMPARE(input::get_cursor()->pos(), QPoint(0, 100));

    // now let's unreserve again
    screenEdges->unreserve(ElectricTopLeft, &callback);
    screenEdges->unreserve(ElectricTop, &callback);
    screenEdges->unreserve(ElectricTopRight, &callback);
    screenEdges->unreserve(ElectricRight, &callback);
    screenEdges->unreserve(ElectricBottomRight, &callback);
    screenEdges->unreserve(ElectricBottom, &callback);
    screenEdges->unreserve(ElectricBottomLeft, &callback);
    screenEdges->unreserve(ElectricLeft, &callback);

    // Some do, some not on Wayland. Needs investigation.
#if 0
    for (auto e: screenEdges->findChildren<Edge*>(QString(), Qt::FindDirectChildrenOnly)) {
        QCOMPARE(e->activatesForPointer(), false);
        QCOMPARE(e->activatesForTouchGesture(), false);
    }
#endif
}

void TestScreenEdges::testCallbackWithCheck()
{
    reset_edger();
    auto& screenEdges = workspace()->edges;

    TestObject callback;
    QSignalSpy spy(&callback, &TestObject::gotCallback);
    QVERIFY(spy.isValid());
    screenEdges->reserve(ElectricLeft, &callback, "callback");

    // check activating a different edge doesn't do anything
    screenEdges->check(QPoint(50, 0), QDateTime::currentDateTimeUtc(), true);
    QVERIFY(spy.isEmpty());

    // try a direct activate without pushback
    input::get_cursor()->set_pos(0, 50);
    screenEdges->check(QPoint(0, 50), QDateTime::currentDateTimeUtc(), true);

    QEXPECT_FAIL("", "Is twice on Wayland. Should be only one. Needs investigation", Continue);
    QCOMPARE(spy.count(), 1);

    QEXPECT_FAIL("", "Cursor moves on other output. Needs investigation.", Continue);
    QCOMPARE(input::get_cursor()->pos(), QPoint(0, 50));

    // use a different edge, this time with pushback
    screenEdges->reserve(KWin::ElectricRight, &callback, "callback");
    input::get_cursor()->set_pos(99, 50);
    screenEdges->check(QPoint(99, 50), QDateTime::currentDateTimeUtc());

    QEXPECT_FAIL("", "Should have been triggered. Needs investigation", Abort);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.last().first().value<ElectricBorder>(), ElectricLeft);

    QEXPECT_FAIL("", "No dead pixel on Wayland? Needs investigation.", Continue);
    QCOMPARE(input::get_cursor()->pos(), QPoint(98, 50));

    input::get_cursor()->set_pos(98, 50);

    // and trigger it again
    QTest::qWait(160);
    input::get_cursor()->set_pos(99, 50);
    screenEdges->check(QPoint(99, 50), QDateTime::currentDateTimeUtc());

    QEXPECT_FAIL("", "Should have been triggered once more. Needs investigation", Continue);
    QCOMPARE(spy.count(), 3);
    QEXPECT_FAIL("", "Follow up", Continue);
    QCOMPARE(spy.last().first().value<ElectricBorder>(), ElectricRight);
    QEXPECT_FAIL("", "Follow up", Continue);
    QCOMPARE(input::get_cursor()->pos(), QPoint(98, 50));
}

void TestScreenEdges::test_overlapping_edges_data()
{
    QTest::addColumn<QRect>("geo1");
    QTest::addColumn<QRect>("geo2");

    QTest::newRow("topleft-1x1") << QRect{0, 1, 1024, 768} << QRect{1, 0, 1024, 768};
    QTest::newRow("left-1x1-same") << QRect{0, 1, 1024, 766} << QRect{1, 0, 1024, 768};
    QTest::newRow("left-1x1-exchanged") << QRect{0, 1, 1024, 768} << QRect{1, 0, 1024, 766};
    QTest::newRow("bottomleft-1x1") << QRect{0, 0, 1024, 768} << QRect{1, 0, 1024, 769};
    QTest::newRow("bottomright-1x1") << QRect{0, 0, 1024, 768} << QRect{0, 0, 1023, 769};
    QTest::newRow("right-1x1-same") << QRect{0, 0, 1024, 768} << QRect{0, 1, 1025, 766};
    QTest::newRow("right-1x1-exchanged") << QRect{0, 0, 1024, 768} << QRect{1, 1, 1024, 768};
}

void TestScreenEdges::test_overlapping_edges()
{
    QSignalSpy changedSpy(&Test::app()->base.screens, &Screens::changed);
    QVERIFY(changedSpy.isValid());

    QFETCH(QRect, geo1);
    QFETCH(QRect, geo2);

    auto const geometries = std::vector<QRect>{geo1, geo2};
    Test::app()->set_outputs(geometries);

    QCOMPARE(changedSpy.count(), geometries.size() + 3);
}

void TestScreenEdges::testPushBack_data()
{
    QTest::addColumn<KWin::ElectricBorder>("border");
    QTest::addColumn<int>("pushback");
    QTest::addColumn<QPoint>("trigger");
    QTest::addColumn<QPoint>("expected");

    QTest::newRow("topleft-3") << KWin::ElectricTopLeft << 3 << QPoint(0, 0) << QPoint(3, 3);
    QTest::newRow("top-5") << KWin::ElectricTop << 5 << QPoint(50, 0) << QPoint(50, 5);
    QTest::newRow("toprigth-2") << KWin::ElectricTopRight << 2 << QPoint(99, 0) << QPoint(97, 2);
    QTest::newRow("right-10") << KWin::ElectricRight << 10 << QPoint(99, 50) << QPoint(89, 50);
    QTest::newRow("bottomright-5")
        << KWin::ElectricBottomRight << 5 << QPoint(99, 99) << QPoint(94, 94);
    QTest::newRow("bottom-10") << KWin::ElectricBottom << 10 << QPoint(50, 99) << QPoint(50, 89);
    QTest::newRow("bottomleft-3") << KWin::ElectricBottomLeft << 3 << QPoint(0, 99)
                                  << QPoint(3, 96);
    QTest::newRow("left-10") << KWin::ElectricLeft << 10 << QPoint(0, 50) << QPoint(10, 50);
    QTest::newRow("invalid") << KWin::ElectricLeft << 10 << QPoint(50, 0) << QPoint(50, 0);
}

void TestScreenEdges::testPushBack()
{
    QFETCH(int, pushback);
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("Windows").writeEntry("ElectricBorderPushbackPixels", pushback);
    config->sync();

    auto const geometries = std::vector<QRect>{{0, 0, 1024, 768}, {200, 768, 1024, 768}};
    Test::app()->set_outputs(geometries);

    reset_edger(config);
    auto& screenEdges = workspace()->edges;
    TestObject callback;
    QSignalSpy spy(&callback, &TestObject::gotCallback);
    QVERIFY(spy.isValid());
    QFETCH(ElectricBorder, border);
    screenEdges->reserve(border, &callback, "callback");

    QFETCH(QPoint, trigger);
    input::get_cursor()->set_pos(trigger);

    QVERIFY(spy.isEmpty());

    // TODO: Does not work for all data at the moment on Wayland.
#if 0
    QTEST(input::get_cursor()->pos(), "expected");

    // do the same without the event, but the check method
    input::get_cursor()->set_pos(trigger);
    screenEdges->check(trigger, QDateTime::currentDateTimeUtc());
    QVERIFY(spy.isEmpty());
    QTEST(input::get_cursor()->pos(), "expected");
#endif
}

void TestScreenEdges::testFullScreenBlocking()
{
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    config->group("Windows").writeEntry("ElectricBorderPushbackPixels", 1);
    config->sync();

    QSignalSpy clientAddedSpy(static_cast<win::wayland::space*>(workspace()),
                              &win::wayland::space::wayland_window_added);
    QVERIFY(clientAddedSpy.isValid());
    auto surface = Test::create_surface();
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);
    Test::render(surface, QSize(100, 50), Qt::blue);
    Test::flush_wayland_connection();
    QVERIFY(clientAddedSpy.wait());
    auto client = workspace()->activeClient();
    QVERIFY(client);

    reset_edger(config);
    auto& screenEdges = workspace()->edges;
    TestObject callback;
    QSignalSpy spy(&callback, &TestObject::gotCallback);
    QVERIFY(spy.isValid());
    screenEdges->reserve(KWin::ElectricLeft, &callback, "callback");
    screenEdges->reserve(KWin::ElectricBottomRight, &callback, "callback");
    QAction action;
    screenEdges->reserveTouch(KWin::ElectricRight, &action);

    // currently there is no active client yet, so check blocking shouldn't do anything
    Q_EMIT screenEdges->checkBlocking();

    for (auto e : screenEdges->findChildren<win::screen_edge*>()) {
        QCOMPARE(e->activatesForTouchGesture(), e->border == KWin::ElectricRight);
    }

    input::get_cursor()->set_pos(0, 50);
    QVERIFY(spy.isEmpty());
    QCOMPARE(input::get_cursor()->pos(), QPoint(1, 50));

    client->setFrameGeometry(Test::app()->base.screens.geometry());
    win::set_active(client, true);
    client->setFullScreen(true);
    workspace()->setActiveClient(client);
    Q_EMIT screenEdges->checkBlocking();

    // the signal doesn't trigger for corners, let's go over all windows just to be sure that it
    // doesn't call for corners
    for (auto e : screenEdges->findChildren<win::screen_edge*>()) {
        e->checkBlocking();
        QCOMPARE(e->activatesForTouchGesture(), e->border == KWin::ElectricRight);
    }
    // calling again should not trigger
    QTest::qWait(160);
    input::get_cursor()->set_pos(0, 50);
    QVERIFY(spy.isEmpty());

    // and no pushback
    QEXPECT_FAIL("", "Does for some reason pushback on Wayland", Continue);
    QCOMPARE(input::get_cursor()->pos(), QPoint(0, 50));

    // let's make the client not fullscreen, which should trigger
    client->setFullScreen(false);
    Q_EMIT screenEdges->checkBlocking();
    for (auto e : screenEdges->findChildren<win::screen_edge*>()) {
        QCOMPARE(e->activatesForTouchGesture(), e->border == KWin::ElectricRight);
    }

    // TODO: Does not trigger for some reason on Wayland.
#if 0
    QVERIFY(!spy.isEmpty());
    QCOMPARE(input::get_cursor()->pos(), QPoint(1, 50));

    // let's make the client fullscreen again, but with a geometry not intersecting the left edge
    QTest::qWait(351);
    client->setFullScreen(true);
    client->setFrameGeometry(client->frameGeometry().translated(10, 0));
    Q_EMIT screenEdges->checkBlocking();
    spy.clear();
    input::get_cursor()->set_pos(0, 50);
    QVERIFY(spy.isEmpty());
    // and a pushback
    QCOMPARE(input::get_cursor()->pos(), QPoint(1, 50));

    // just to be sure, let's set geometry back
    client->setFrameGeometry(Test::app()->base.screens.geometry());
    Q_EMIT screenEdges->checkBlocking();
    input::get_cursor()->set_pos(0, 50);
    QVERIFY(spy.isEmpty());
    // and no pushback
    QCOMPARE(input::get_cursor()->pos(), QPoint(0, 50));

    // the corner should always trigger
    screenEdges->unreserve(KWin::ElectricLeft, &callback);
    input::get_cursor()->set_pos(99, 99);
    QVERIFY(spy.isEmpty());

    // and pushback
    QCOMPARE(input::get_cursor()->pos(), QPoint(98, 98));
    QTest::qWait(160);
    input::get_cursor()->set_pos(99, 99);
    QVERIFY(!spy.isEmpty());
#endif
}

void TestScreenEdges::testClientEdge()
{
    QSignalSpy clientAddedSpy(static_cast<win::wayland::space*>(workspace()),
                              &win::wayland::space::wayland_window_added);
    QVERIFY(clientAddedSpy.isValid());
    auto surface = Test::create_surface();
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);
    Test::render(surface, QSize(100, 50), Qt::blue);
    Test::flush_wayland_connection();
    QVERIFY(clientAddedSpy.wait());
    auto client = workspace()->activeClient();
    QVERIFY(client);

    client->setFrameGeometry(QRect(10, 50, 10, 50));

    reset_edger();
    auto& screenEdges = workspace()->edges;

    screenEdges->reserve(client, KWin::ElectricBottom);

    auto edge = screenEdges->findChildren<win::screen_edge*>().last();

    QEXPECT_FAIL("", "This changed recently. Needs investigation.", Continue);
    QCOMPARE(edge->reserved_count > 0, true);
    QCOMPARE(edge->activatesForPointer(), true);
    QCOMPARE(edge->activatesForTouchGesture(), false);

    // remove old reserves and resize to be in the middle of the screen
    screenEdges->reserve(client, KWin::ElectricNone);
    client->setFrameGeometry(QRect(2, 2, 20, 20));

    // for none of the edges it should be able to be set
    for (int i = 0; i < ELECTRIC_COUNT; ++i) {
        client->hideClient(true);
        screenEdges->reserve(client, static_cast<ElectricBorder>(i));

        QEXPECT_FAIL("", "Possible on Wayland. Needs investigation.", Continue);
        QCOMPARE(client->isHiddenInternal(), false);
    }

    // now let's try to set it and activate it
    client->setFrameGeometry(Test::app()->base.screens.geometry());
    client->hideClient(true);
    screenEdges->reserve(client, KWin::ElectricLeft);
    QCOMPARE(client->isHiddenInternal(), true);

    input::get_cursor()->set_pos(0, 50);

    // autohiding panels shall activate instantly
    QEXPECT_FAIL("", "Is hidden on Wayland but was not on X11. Needs investigation.", Abort);
    QCOMPARE(client->isHiddenInternal(), false);
    QCOMPARE(input::get_cursor()->pos(), QPoint(1, 50));

    // now let's reserve the client for each of the edges, in the end for the right one
    client->hideClient(true);
    screenEdges->reserve(client, KWin::ElectricTop);
    screenEdges->reserve(client, KWin::ElectricBottom);
    QCOMPARE(client->isHiddenInternal(), true);

    // corners shouldn't get reserved
    screenEdges->reserve(client, KWin::ElectricTopLeft);
    QCOMPARE(client->isHiddenInternal(), false);
    client->hideClient(true);
    screenEdges->reserve(client, KWin::ElectricTopRight);
    QCOMPARE(client->isHiddenInternal(), false);
    client->hideClient(true);
    screenEdges->reserve(client, KWin::ElectricBottomRight);
    QCOMPARE(client->isHiddenInternal(), false);
    client->hideClient(true);
    screenEdges->reserve(client, KWin::ElectricBottomLeft);
    QCOMPARE(client->isHiddenInternal(), false);

    // now finally reserve on right one
    client->hideClient(true);
    screenEdges->reserve(client, KWin::ElectricRight);
    QCOMPARE(client->isHiddenInternal(), true);

    // now let's emulate the removal of a Client through Workspace
    Q_EMIT workspace()->clientRemoved(client);
    for (auto e : screenEdges->findChildren<win::screen_edge*>()) {
        QVERIFY(!e->client());
    }
    QCOMPARE(client->isHiddenInternal(), true);

    // now let's try to trigger the client showing with the check method instead of enter notify
    screenEdges->reserve(client, KWin::ElectricTop);
    QCOMPARE(client->isHiddenInternal(), true);
    input::get_cursor()->set_pos(50, 0);
    screenEdges->check(QPoint(50, 0), QDateTime::currentDateTimeUtc());
    QCOMPARE(client->isHiddenInternal(), false);
    QCOMPARE(input::get_cursor()->pos(), QPoint(50, 1));

    // unreserve by setting to none edge
    screenEdges->reserve(client, KWin::ElectricNone);
    // check on previous edge again, should fail
    client->hideClient(true);
    input::get_cursor()->set_pos(50, 0);
    screenEdges->check(QPoint(50, 0), QDateTime::currentDateTimeUtc());
    QCOMPARE(client->isHiddenInternal(), true);
    QCOMPARE(input::get_cursor()->pos(), QPoint(50, 0));

    // set to windows can cover
    client->setFrameGeometry(Test::app()->base.screens.geometry());
    client->hideClient(false);
    win::set_keep_below(client, true);
    screenEdges->reserve(client, KWin::ElectricLeft);
    QCOMPARE(client->control->keep_below(), true);
    QCOMPARE(client->isHiddenInternal(), false);

    input::get_cursor()->set_pos(0, 50);
    QCOMPARE(client->control->keep_below(), false);
    QCOMPARE(client->isHiddenInternal(), false);
    QCOMPARE(input::get_cursor()->pos(), QPoint(1, 50));
}

void TestScreenEdges::testTouchEdge()
{
    qRegisterMetaType<KWin::ElectricBorder>("ElectricBorder");
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    auto group = config->group("TouchEdges");
    group.writeEntry("Top", "krunner");
    group.writeEntry("Left", "krunner");
    group.writeEntry("Bottom", "krunner");
    group.writeEntry("Right", "krunner");
    config->sync();

    reset_edger(config);
    auto& screenEdges = workspace()->edges;

    // we don't have multiple desktops, so it's returning false
    QEXPECT_FAIL("", "Possible on Wayland. Needs investigation.", Abort);
    QCOMPARE(screenEdges->desktop_switching.always, false);
    QCOMPARE(screenEdges->desktop_switching.when_moving_client, false);
    QCOMPARE(screenEdges->actions.top_left, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.top, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.top_right, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.right, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.bottom_right, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.bottom, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.bottom_left, ElectricBorderAction::ElectricActionNone);
    QCOMPARE(screenEdges->actions.left, ElectricBorderAction::ElectricActionNone);

    auto edges
        = screenEdges->findChildren<win::screen_edge*>(QString(), Qt::FindDirectChildrenOnly);
    QCOMPARE(edges.size(), 8);

    // TODO: Does not pass for all edges at the moment on Wayland.
#if 0
    for (auto e : edges) {
        QCOMPARE(e->reserved_count > 0, e->isScreenEdge());
        QCOMPARE(e->activatesForPointer(), false);
        QCOMPARE(e->activatesForTouchGesture(), e->isScreenEdge());
    }
#endif

    // try to activate the edge through pointer, should not be possible
    auto it = std::find_if(edges.constBegin(), edges.constEnd(), [](auto e) {
        return e->isScreenEdge() && e->isLeft();
    });
    QVERIFY(it != edges.constEnd());

    QSignalSpy approachingSpy(screenEdges.get(), &win::screen_edger::approaching);
    QVERIFY(approachingSpy.isValid());

    auto setPos = [](const QPoint& pos) { input::get_cursor()->set_pos(pos); };
    setPos(QPoint(0, 50));
    QVERIFY(approachingSpy.isEmpty());
    // let's also verify the check
    screenEdges->check(QPoint(0, 50), QDateTime::currentDateTimeUtc(), false);
    QVERIFY(approachingSpy.isEmpty());

    screenEdges->gesture_recognizer->startSwipeGesture(QPoint(0, 50));
    QCOMPARE(approachingSpy.count(), 1);
    screenEdges->gesture_recognizer->cancelSwipeGesture();
    QCOMPARE(approachingSpy.count(), 2);

    // let's reconfigure
    group.writeEntry("Top", "none");
    group.writeEntry("Left", "none");
    group.writeEntry("Bottom", "none");
    group.writeEntry("Right", "none");
    config->sync();
    screenEdges->reconfigure();

    edges = screenEdges->findChildren<win::screen_edge*>(QString(), Qt::FindDirectChildrenOnly);
    QCOMPARE(edges.size(), 8);

    // TODO: Does not pass for all edges at the moment on Wayland.
#if 0
    for (auto e : edges) {
        QCOMPARE(e->reserved_count, 0);
        QCOMPARE(e->activatesForPointer(), false);
        QCOMPARE(e->activatesForTouchGesture(), false);
    }
#endif
}

void TestScreenEdges::testTouchCallback_data()
{
    QTest::addColumn<KWin::ElectricBorder>("border");
    QTest::addColumn<QPoint>("startPos");
    QTest::addColumn<QSizeF>("delta");

    QTest::newRow("left") << KWin::ElectricLeft << QPoint(0, 50) << QSizeF(250, 20);
    QTest::newRow("top") << KWin::ElectricTop << QPoint(50, 0) << QSizeF(20, 250);
    QTest::newRow("right") << KWin::ElectricRight << QPoint(99, 50) << QSizeF(-200, 0);
    QTest::newRow("bottom") << KWin::ElectricBottom << QPoint(50, 99) << QSizeF(0, -200);
}

void TestScreenEdges::testTouchCallback()
{
    qRegisterMetaType<KWin::ElectricBorder>("ElectricBorder");
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    auto group = config->group("TouchEdges");
    group.writeEntry("Top", "none");
    group.writeEntry("Left", "none");
    group.writeEntry("Bottom", "none");
    group.writeEntry("Right", "none");
    config->sync();

    reset_edger(config);
    auto& screenEdges = workspace()->edges;

    // none of our actions should be reserved
    auto edges
        = screenEdges->findChildren<win::screen_edge*>(QString(), Qt::FindDirectChildrenOnly);

    QEXPECT_FAIL("", "On Wayland these are 10 suddenly. Needs investigation.", Continue);
    QCOMPARE(edges.size(), 8);
    QCOMPARE(edges.size(), 10);

    // TODO: Does not pass for all edges at the moment on Wayland.
#if 0
    for (auto e : edges) {
        QCOMPARE(e->reserved_count, 0);
        QCOMPARE(e->activatesForPointer(), false);
        QCOMPARE(e->activatesForTouchGesture(), false);
    }
#endif

    // let's reserve an action
    QAction action;
    QSignalSpy actionTriggeredSpy(&action, &QAction::triggered);
    QVERIFY(actionTriggeredSpy.isValid());
    QSignalSpy approachingSpy(screenEdges.get(), &win::screen_edger::approaching);
    QVERIFY(approachingSpy.isValid());

    // reserve on edge
    QFETCH(KWin::ElectricBorder, border);
    screenEdges->reserveTouch(border, &action);

    // TODO: Does not pass for all edges at the moment on Wayland.
#if 0
    for (auto e : edges) {
        QCOMPARE(e->reserved_count > 0, e->border == border);
        QCOMPARE(e->activatesForPointer(), false);
        QCOMPARE(e->activatesForTouchGesture(), e->border == border);
    }
#endif

    QEXPECT_FAIL("", "Does not work on Wayland like before on X11. Needs fixing.", Abort);
    QVERIFY(false);

    QVERIFY(approachingSpy.isEmpty());
    QFETCH(QPoint, startPos);
    QCOMPARE(screenEdges->gesture_recognizer->startSwipeGesture(startPos), 1);
    QVERIFY(actionTriggeredSpy.isEmpty());
    QCOMPARE(approachingSpy.count(), 1);
    QFETCH(QSizeF, delta);
    screenEdges->gesture_recognizer->updateSwipeGesture(delta);
    QCOMPARE(approachingSpy.count(), 2);
    QVERIFY(actionTriggeredSpy.isEmpty());
    screenEdges->gesture_recognizer->endSwipeGesture();

    QVERIFY(actionTriggeredSpy.wait());
    QCOMPARE(actionTriggeredSpy.count(), 1);
    QCOMPARE(approachingSpy.count(), 3);

    // unreserve again
    screenEdges->unreserveTouch(border, &action);
    for (auto e : edges) {
        QCOMPARE(e->reserved_count, 0);
        QCOMPARE(e->activatesForPointer(), false);
        QCOMPARE(e->activatesForTouchGesture(), false);
    }

    // reserve another action
    std::unique_ptr<QAction> action2(new QAction);
    screenEdges->reserveTouch(border, action2.get());
    for (auto e : edges) {
        QCOMPARE(e->reserved_count > 0, e->border == border);
        QCOMPARE(e->activatesForPointer(), false);
        QCOMPARE(e->activatesForTouchGesture(), e->border == border);
    }
    // and unreserve by destroying
    action2.reset();
    for (auto e : edges) {
        QCOMPARE(e->reserved_count, 0);
        QCOMPARE(e->activatesForPointer(), false);
        QCOMPARE(e->activatesForTouchGesture(), false);
    }
}

}

WAYLANDTEST_MAIN(KWin::TestScreenEdges)
#include "screen_edges.moc"
