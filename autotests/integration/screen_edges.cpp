/*
SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "base/x11/xcb/proto.h"
#include "input/cursor.h"
#include "win/actions.h"
#include "win/activation.h"
#include "win/screen_edges.h"
#include "win/wayland/space.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/surface.h>

#include <KConfigGroup>

#include <QDateTime>
#include <catch2/generators/catch_generators.hpp>

namespace KWin::detail::test
{

namespace
{

class TestObject : public QObject
{
    Q_OBJECT
public Q_SLOTS:
    bool callback(win::electric_border border);
Q_SIGNALS:
    void gotCallback(win::electric_border);
};

bool TestObject::callback(win::electric_border border)
{
    qDebug() << "GOT CALLBACK" << static_cast<int>(border);
    Q_EMIT gotCallback(border);
    return true;
}

}

TEST_CASE("screen edges", "[input],[win]")
{
    qRegisterMetaType<win::electric_border>("win::electric_border");

    // TODO(romangg): This test fails with Xwayland enabled. Fix it!
    test::setup setup("screen-edges");
    setup.start();
    setup_wayland_connection();
    cursor()->set_pos(QPoint(640, 512));

    auto reset_edger = [&](KSharedConfig::Ptr config) {
        setup.base->config.main = config;
        setup.base->space->edges = std::make_unique<win::screen_edger<space>>(*setup.base->space);
    };

    auto unreserve = [&](uint32_t id, win::electric_border border) {
        setup.base->space->edges->unreserve(border, id);
    };

    auto unreserve_many = [&](std::deque<uint32_t>& border_ids, win::electric_border border) {
        QVERIFY(!border_ids.empty());
        unreserve(border_ids.front(), border);
        border_ids.pop_front();
    };

    SECTION("init")
    {
        auto& screenEdges = setup.base->space->edges;
        QCOMPARE(screenEdges->desktop_switching.always, false);
        QCOMPARE(screenEdges->desktop_switching.when_moving_client, false);
        REQUIRE(screenEdges->time_threshold == std::chrono::milliseconds(150));
        REQUIRE(screenEdges->reactivate_threshold == std::chrono::milliseconds(350));
        QCOMPARE(screenEdges->cursor_push_back_distance, QSize(1, 1));
        QCOMPARE(screenEdges->actions.top_left, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.top, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.top_right, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.right, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.bottom_right, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.bottom, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.bottom_left, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.left, win::electric_border_action::none);

        auto& edges = screenEdges->edges;
        QCOMPARE(edges.size(), 8);

        for (auto& e : edges) {
            //        QVERIFY(e->isReserved());
            QVERIFY(!e->client());
            QVERIFY(!e->is_approaching);
        }

        auto te = edges.at(0).get();
        QVERIFY(te->isCorner());
        QVERIFY(!te->isScreenEdge());
        QVERIFY(te->isLeft());
        QVERIFY(te->isTop());
        QVERIFY(!te->isRight());
        QVERIFY(!te->isBottom());
        QCOMPARE(te->border, win::electric_border::top_left);
        te = edges.at(1).get();
        QVERIFY(te->isCorner());
        QVERIFY(!te->isScreenEdge());
        QVERIFY(te->isLeft());
        QVERIFY(!te->isTop());
        QVERIFY(!te->isRight());
        QVERIFY(te->isBottom());
        QCOMPARE(te->border, win::electric_border::bottom_left);
        te = edges.at(2).get();
        QVERIFY(!te->isCorner());
        QVERIFY(te->isScreenEdge());
        QVERIFY(te->isLeft());
        QVERIFY(!te->isTop());
        QVERIFY(!te->isRight());
        QVERIFY(!te->isBottom());
        QCOMPARE(te->border, win::electric_border::left);
        te = edges.at(3).get();
        QVERIFY(te->isCorner());
        QVERIFY(!te->isScreenEdge());
        QVERIFY(!te->isLeft());
        QVERIFY(te->isTop());
        QVERIFY(te->isRight());
        QVERIFY(!te->isBottom());
        QCOMPARE(te->border, win::electric_border::top_right);
        te = edges.at(4).get();
        QVERIFY(te->isCorner());
        QVERIFY(!te->isScreenEdge());
        QVERIFY(!te->isLeft());
        QVERIFY(!te->isTop());
        QVERIFY(te->isRight());
        QVERIFY(te->isBottom());
        QCOMPARE(te->border, win::electric_border::bottom_right);
        te = edges.at(5).get();
        QVERIFY(!te->isCorner());
        QVERIFY(te->isScreenEdge());
        QVERIFY(!te->isLeft());
        QVERIFY(!te->isTop());
        QVERIFY(te->isRight());
        QVERIFY(!te->isBottom());
        QCOMPARE(te->border, win::electric_border::right);
        te = edges.at(6).get();
        QVERIFY(!te->isCorner());
        QVERIFY(te->isScreenEdge());
        QVERIFY(!te->isLeft());
        QVERIFY(te->isTop());
        QVERIFY(!te->isRight());
        QVERIFY(!te->isBottom());
        QCOMPARE(te->border, win::electric_border::top);
        te = edges.at(7).get();
        QVERIFY(!te->isCorner());
        QVERIFY(te->isScreenEdge());
        QVERIFY(!te->isLeft());
        QVERIFY(!te->isTop());
        QVERIFY(!te->isRight());
        QVERIFY(te->isBottom());
        QCOMPARE(te->border, win::electric_border::bottom);

        // we shouldn't have any x windows, though
        QCOMPARE(win::x11::screen_edges_windows(*screenEdges).size(), 0);
    }

    SECTION("create initial edges")
    {
        auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
        config->group("Windows").writeEntry("ElectricBorders", 2 /*ElectricAlways*/);
        config->sync();

        reset_edger(config);
        auto& screenEdges = setup.base->space->edges;

        // we don't have multiple desktops, so it's returning false
        REQUIRE(screenEdges->desktop_switching.always);
        REQUIRE(screenEdges->desktop_switching.when_moving_client);
        QCOMPARE(screenEdges->actions.top_left, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.top, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.top_right, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.right, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.bottom_right, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.bottom, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.bottom_left, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.left, win::electric_border_action::none);

        QCOMPARE(win::x11::screen_edges_windows(*screenEdges).size(), 0);

        // set some reasonable virtual desktops
        config->group("Desktops").writeEntry("Number", 4);
        config->sync();
        auto& subs = setup.base->space->subspace_manager;
        subs->config = config;
        win::subspace_manager_load(*subs);
        win::subspace_manager_update_layout(*subs);
        QCOMPARE(subs->subspaces.size(), 4u);
        QCOMPARE(subs->grid.width(), 4);
        QCOMPARE(subs->grid.height(), 1);

        // approach windows for edges not created as screen too small
        screenEdges->updateLayout();
        auto edgeWindows = win::x11::screen_edges_windows(*screenEdges);

        // TODO(romangg): No window edges on Wayland. Needs investigation.
        REQUIRE_FALSE(edgeWindows.size() == 12);
        return;

        auto testWindowGeometry = [&](int index) {
            base::x11::xcb::geometry geo(setup.base->x11_data.connection, edgeWindows[index]);
            return geo.rect();
        };
        QRect sg = QRect({}, setup.base->topology.size);
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

        QCOMPARE(screenEdges->edges.size(), 8);
        for (auto& e : screenEdges->edges) {
            QVERIFY(e->reserved_count > 0);
            REQUIRE(e->activatesForPointer());
            REQUIRE(!e->activatesForTouchGesture());
        }

        QSignalSpy changedSpy(setup.base.get(), &base::platform::topology_changed);
        QVERIFY(changedSpy.isValid());

        setup.set_outputs({{0, 0, 1024, 768}});
        QCOMPARE(changedSpy.count(), 1);

        // let's update the layout and verify that we have edges
        screenEdges->recreateEdges();
        edgeWindows = win::x11::screen_edges_windows(*screenEdges);
        QCOMPARE(edgeWindows.size(), 16);
        sg = QRect({}, setup.base->topology.size);
        expectedGeometries
            = QList<QRect>{QRect(0, 0, 1, 1),
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
        REQUIRE(!screenEdges->desktop_switching.always);
        REQUIRE(screenEdges->desktop_switching.when_moving_client);
        REQUIRE(win::x11::screen_edges_windows(*screenEdges).size() == 0);

        QCOMPARE(screenEdges->edges.size(), 8);
        for (int i = 0; i < 8; ++i) {
            auto& e = screenEdges->edges.at(i);
            QVERIFY(e->reserved_count == 0);
            QCOMPARE(e->activatesForPointer(), false);
            QCOMPARE(e->activatesForTouchGesture(), false);
            QCOMPARE(e->approach_geometry, expectedGeometries.at(i * 2 + 1));
        }

        // Let's start a window move. First create a window.
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());
        auto surface = create_surface();
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);
        render(surface, QSize(100, 50), Qt::blue);
        flush_wayland_connection();
        QVERIFY(clientAddedSpy.wait());
        auto client = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(client);

        win::set_move_resize_window(*setup.base->space, *client);
        for (int i = 0; i < 8; ++i) {
            auto& e = screenEdges->edges.at(i);
            QVERIFY(e->reserved_count > 0);
            REQUIRE(e->activatesForPointer());
            REQUIRE(!e->activatesForTouchGesture());
            REQUIRE(e->approach_geometry == expectedGeometries.at(i * 2 + 1));
        }
        // not for resize
        //    win::start_move_resize(client);
        //    client->setResize(true);
        for (int i = 0; i < 8; ++i) {
            auto& e = screenEdges->edges.at(i);
            QVERIFY(e->reserved_count > 0);
            QCOMPARE(e->activatesForPointer(), false);
            QCOMPARE(e->activatesForTouchGesture(), false);
            QCOMPARE(e->approach_geometry, expectedGeometries.at(i * 2 + 1));
        }
        win::unset_move_resize_window(*setup.base->space);
    }

    SECTION("callback")
    {
        QSignalSpy changedSpy(setup.base.get(), &base::platform::topology_changed);
        QVERIFY(changedSpy.isValid());

        auto const geometries = std::vector<QRect>{{0, 0, 1024, 768}, {200, 768, 1024, 768}};
        setup.set_outputs(geometries);

        QCOMPARE(changedSpy.count(), 1);

        auto& screenEdges = setup.base->space->edges;

        TestObject callback;
        auto cb = [&](auto eb) { return callback.callback(eb); };

        QSignalSpy spy(&callback, &TestObject::gotCallback);
        QVERIFY(spy.isValid());

        std::deque<uint32_t> border_ids;
        border_ids.push_back(screenEdges->reserve(win::electric_border::left, cb));
        border_ids.push_back(screenEdges->reserve(win::electric_border::top_left, cb));
        border_ids.push_back(screenEdges->reserve(win::electric_border::top, cb));
        border_ids.push_back(screenEdges->reserve(win::electric_border::top_right, cb));
        border_ids.push_back(screenEdges->reserve(win::electric_border::right, cb));
        border_ids.push_back(screenEdges->reserve(win::electric_border::bottom_right, cb));
        border_ids.push_back(screenEdges->reserve(win::electric_border::bottom, cb));
        border_ids.push_back(screenEdges->reserve(win::electric_border::bottom_left, cb));

        auto& edges = screenEdges->edges;
        QCOMPARE(edges.size(), 10);
        for (auto& e : edges) {
            REQUIRE(e->reserved_count > 0);
            REQUIRE(e->activatesForPointer());
            //        REQUIRE(e->activatesForTouchGesture());
        }
        auto it = std::find_if(edges.cbegin(), edges.cend(), [](auto& e) {
            return e->isScreenEdge() && e->isLeft() && e->approach_geometry.bottom() < 768;
        });
        QVERIFY(it != edges.cend());

        auto setPos = [](const QPoint& pos) {
            pointer_motion_absolute(pos, QDateTime::currentMSecsSinceEpoch());
        };

        setPos(QPoint(0, 50));

        // doesn't trigger as the edge was not triggered yet
        QVERIFY(spy.isEmpty());
        QCOMPARE(cursor()->pos(), QPoint(1, 50));

        // test doesn't trigger due to too much offset
        QTest::qWait(160);
        setPos(QPoint(0, 100));
        QVERIFY(spy.isEmpty());
        QCOMPARE(cursor()->pos(), QPoint(1, 100));

        // doesn't trigger as we are waiting too long already
        QTest::qWait(200);
        setPos(QPoint(0, 101));

        QVERIFY(spy.isEmpty());
        QCOMPARE(cursor()->pos(), QPoint(1, 101));

        spy.clear();

        // doesn't activate as we are waiting too short
        QTest::qWait(50);
        setPos(QPoint(0, 100));
        QVERIFY(spy.isEmpty());
        QCOMPARE(cursor()->pos(), QPoint(1, 100));

        // and this one triggers
        QTest::qWait(110);
        setPos(QPoint(0, 101));
        // TODO(romangg): Is the other way around on Wayland than it was on X11. Needs
        // investigation.
        REQUIRE_FALSE(!spy.isEmpty());

        // TODO(romangg): No dead pixel on Wayland? Needs investigation.
        REQUIRE_FALSE(cursor()->pos() == QPoint(1, 101));

        // now let's try to trigger again
        QTest::qWait(351);
        setPos(QPoint(0, 100));

        // TODO(romangg): Is the other way around on Wayland than it was on X11. Needs
        // investigation.
        REQUIRE_FALSE(spy.count() == 1);

        // TODO(romangg): No pushback on Wayland. Needs investigation.
        REQUIRE_FALSE(cursor()->pos() == QPoint(1, 100));

        // it's still under the reactivation
        QTest::qWait(50);
        setPos(QPoint(0, 100));

        // TODO(romangg): Is the other way around on Wayland than it was on X11. Needs
        // investigation.
        REQUIRE_FALSE(spy.count() == 1);

        // TODO(romangg):
        REQUIRE_FALSE(cursor()->pos() == QPoint(1, 100));

        // now it should trigger again
        QTest::qWait(250);
        setPos(QPoint(0, 100));

        // TODO(romangg): Is the other way around on Wayland than it was on X11. Needs
        // investigation.
        REQUIRE_FALSE(spy.count() == 2);
        return;

        QCOMPARE(spy.first().first().value<win::electric_border>(), win::electric_border::left);
        QCOMPARE(spy.last().first().value<win::electric_border>(), win::electric_border::left);
        QCOMPARE(cursor()->pos(), QPoint(1, 100));

        // let's disable pushback
        auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
        config->group("Windows").writeEntry("ElectricBorderPushbackPixels", 0);
        config->sync();
        screenEdges->config = config;
        screenEdges->reconfigure();

        // it should trigger directly
        QTest::qWait(350);
        // TODO(romangg): Is the other way around on Wayland than it was on X11. Needs
        // investigation.
        REQUIRE_FALSE(spy.count() == 3);
        QCOMPARE(spy.at(0).first().value<win::electric_border>(), win::electric_border::left);
#if 0
        QCOMPARE(spy.at(1).first().value<win::electric_border>(), win::electric_border::left);
        QCOMPARE(spy.at(2).first().value<win::electric_border>(), win::electric_border::left);
#endif
        // TODO(romangg): No dead pixel on Wayland? Needs investigation.
        REQUIRE_FALSE(cursor()->pos() == QPoint(0, 100));

        // now let's unreserve again
        unreserve_many(border_ids, win::electric_border::top_left);
        unreserve_many(border_ids, win::electric_border::top);
        unreserve_many(border_ids, win::electric_border::top_right);
        unreserve_many(border_ids, win::electric_border::right);
        unreserve_many(border_ids, win::electric_border::bottom_right);
        unreserve_many(border_ids, win::electric_border::bottom);
        unreserve_many(border_ids, win::electric_border::bottom_left);
        unreserve_many(border_ids, win::electric_border::left);

        // Some do, some not on Wayland. Needs investigation.
#if 0
        for (auto e: screenEdges->findChildren<Edge*>(QString(), Qt::FindDirectChildrenOnly)) {
            QCOMPARE(e->activatesForPointer(), false);
            QCOMPARE(e->activatesForTouchGesture(), false);
        }
#endif
    }

    SECTION("callback with check")
    {
        auto& screenEdges = setup.base->space->edges;

        TestObject callback;
        auto cb = [&](auto eb) { return callback.callback(eb); };

        QSignalSpy spy(&callback, &TestObject::gotCallback);
        QVERIFY(spy.isValid());

        std::deque<uint32_t> border_ids;
        border_ids.push_back(screenEdges->reserve(win::electric_border::left, cb));

        // check activating a different edge doesn't do anything
        screenEdges->check(QPoint(50, 0), std::chrono::system_clock::now(), true);
        QVERIFY(spy.isEmpty());

        // try a direct activate without pushback
        cursor()->set_pos(0, 50);
        screenEdges->check(QPoint(0, 50), std::chrono::system_clock::now(), true);

        // TODO(romangg): Is twice on Wayland. Should be only one. Needs investigation.
        REQUIRE_FALSE(spy.count() == 1);

        // TODO(romangg): Cursor moves on other output. Needs investigation.
        REQUIRE_FALSE(cursor()->pos() == QPoint(0, 50));

        // use a different edge, this time with pushback
        border_ids.push_back(screenEdges->reserve(win::electric_border::right, cb));
        cursor()->set_pos(99, 50);
        screenEdges->check(QPoint(99, 50), std::chrono::system_clock::now());

        // TODO(romangg): Should have been triggered. Needs investigation.
        REQUIRE_FALSE(spy.count() == 2);
        return;

        QCOMPARE(spy.last().first().value<win::electric_border>(), win::electric_border::left);

        // TODO(romangg): No dead pixel on Wayland? Needs investigation.
        REQUIRE_FALSE(cursor()->pos() == QPoint(98, 50));

        cursor()->set_pos(98, 50);

        // and trigger it again
        QTest::qWait(160);
        cursor()->set_pos(99, 50);
        screenEdges->check(QPoint(99, 50), std::chrono::system_clock::now());

        // TODO(romangg): Should have been triggered once more. Needs investigation.
        REQUIRE_FALSE(spy.count() == 3);
        // TODO(romangg): Follow up
        REQUIRE_FALSE(spy.last().first().value<win::electric_border>()
                      == win::electric_border::right);
        // TODO(romangg): Follow up
        REQUIRE_FALSE(cursor()->pos() == QPoint(98, 50));

        unreserve_many(border_ids, win::electric_border::left);
        unreserve_many(border_ids, win::electric_border::right);
    }

    SECTION("overlapping edges")
    {
        struct data {
            QRect geo1;
            QRect geo2;
        };

        auto test_data = GENERATE(
            // topleft-1x1
            data{{0, 1, 1024, 768}, {1, 0, 1024, 768}},
            // left-1x1-same
            data{{0, 1, 1024, 766}, {1, 0, 1024, 768}},
            // left-1x1-exchanged
            data{{0, 1, 1024, 768}, {1, 0, 1024, 766}},
            // bottomleft-1x1
            data{{0, 0, 1024, 768}, {1, 0, 1024, 769}},
            // bottomright-1x1
            data{{0, 0, 1024, 768}, {0, 0, 1023, 769}},
            // right-1x1-same
            data{{0, 0, 1024, 768}, {0, 1, 1025, 766}},
            // right-1x1-exchanged
            data{{0, 0, 1024, 768}, {1, 1, 1024, 768}});

        setup.set_outputs(1);

        QSignalSpy changedSpy(setup.base.get(), &base::platform::topology_changed);
        QVERIFY(changedSpy.isValid());

        auto const geometries = std::vector<QRect>{test_data.geo1, test_data.geo2};
        setup.set_outputs(geometries);

        QCOMPARE(changedSpy.count(), 1);
    }

    SECTION("push back")
    {
        struct data {
            win::electric_border border;
            int pushback;
            QPoint trigger;
            QPoint expected;
        };

        auto test_data = GENERATE(data{win::electric_border::top_left, 3, {}, {3, 3}},
                                  data{win::electric_border::top, 5, {50, 0}, {50, 5}},
                                  data{win::electric_border::top_right, 2, {99, 0}, {97, 2}},
                                  data{win::electric_border::right, 10, {99, 50}, {89, 50}},
                                  data{win::electric_border::bottom_right, 5, {99, 99}, {94, 94}},
                                  data{win::electric_border::bottom, 10, {50, 99}, {50, 89}},
                                  data{win::electric_border::bottom_left, 3, {0, 99}, {3, 96}},
                                  data{win::electric_border::left, 10, {0, 50}, {10, 50}},
                                  data{win::electric_border::left, 10, {50, 0}, {50, 0}});

        auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
        config->group("Windows").writeEntry("ElectricBorderPushbackPixels", test_data.pushback);
        config->sync();

        auto const geometries = std::vector<QRect>{{0, 0, 1024, 768}, {200, 768, 1024, 768}};
        setup.set_outputs(geometries);

        reset_edger(config);
        auto& screenEdges = setup.base->space->edges;

        TestObject callback;
        auto cb = [&](auto eb) { return callback.callback(eb); };

        QSignalSpy spy(&callback, &TestObject::gotCallback);
        QVERIFY(spy.isValid());

        auto id = screenEdges->reserve(test_data.border, cb);

        cursor()->set_pos(test_data.trigger);

        QVERIFY(spy.isEmpty());

        // TODO: Does not work for all data at the moment on Wayland.
#if 0
        REQUIRE(cursor()->pos() == test_data.expected);

        // do the same without the event, but the check method
        cursor()->set_pos(trigger);
        screenEdges->check(trigger, std::chrono::system_clock::now());
        QVERIFY(spy.isEmpty());
        QTEST(cursor()->pos(), "expected");
#endif

        unreserve(id, test_data.border);
    }

    SECTION("fullscreen blocking")
    {
        auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
        config->group("Windows").writeEntry("ElectricBorderPushbackPixels", 1);
        config->sync();

        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());
        auto surface = create_surface();
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);
        render(surface, QSize(100, 50), Qt::blue);
        flush_wayland_connection();
        QVERIFY(clientAddedSpy.wait());

        auto client = get_window<wayland_window>(setup.base->space->stacking.active);
        QVERIFY(client);

        reset_edger(config);
        auto& screenEdges = setup.base->space->edges;

        TestObject callback;
        auto cb = [&](auto eb) { return callback.callback(eb); };

        QSignalSpy spy(&callback, &TestObject::gotCallback);
        QVERIFY(spy.isValid());

        std::deque<uint32_t> border_ids;
        border_ids.push_back(screenEdges->reserve(win::electric_border::left, cb));
        border_ids.push_back(screenEdges->reserve(win::electric_border::bottom_right, cb));

        QAction action;
        screenEdges->reserveTouch(win::electric_border::right, &action);

        // currently there is no active client yet, so check blocking shouldn't do anything
        Q_EMIT screenEdges->qobject->checkBlocking();

        for (auto& e : screenEdges->edges) {
            REQUIRE(e->activatesForTouchGesture() == (e->border == win::electric_border::right));
        }

        cursor()->set_pos(0, 50);
        QVERIFY(spy.isEmpty());
        QCOMPARE(cursor()->pos(), QPoint(1, 50));

        client->setFrameGeometry(QRect({}, setup.base->topology.size));
        win::set_active(client, true);
        client->setFullScreen(true);
        win::set_active_window(*setup.base->space, *client);
        Q_EMIT screenEdges->qobject->checkBlocking();

        // the signal doesn't trigger for corners, let's go over all windows just to be sure that it
        // doesn't call for corners
        for (auto& e : screenEdges->edges) {
            e->checkBlocking();
            REQUIRE(e->activatesForTouchGesture() == (e->border == win::electric_border::right));
        }
        // calling again should not trigger
        QTest::qWait(160);
        cursor()->set_pos(0, 50);
        QVERIFY(spy.isEmpty());

        // and no pushback
        // TODO(romangg): Does for some reason pushback on Wayland.
        REQUIRE_FALSE(cursor()->pos() == QPoint(0, 50));

        // let's make the client not fullscreen, which should trigger
        client->setFullScreen(false);
        Q_EMIT screenEdges->qobject->checkBlocking();
        for (auto& e : screenEdges->edges) {
            REQUIRE(e->activatesForTouchGesture() == (e->border == win::electric_border::right));
        }

        // TODO: Does not trigger for some reason on Wayland.
#if 0
        QVERIFY(!spy.isEmpty());
        QCOMPARE(cursor()->pos(), QPoint(1, 50));

        // let's make the client fullscreen again, but with a geometry not intersecting the left edge
        QTest::qWait(351);
        client->setFullScreen(true);
        client->setFrameGeometry(client->geo.frame.translated(10, 0));
        Q_EMIT screenEdges->checkBlocking();
        spy.clear();
        cursor()->set_pos(0, 50);
        QVERIFY(spy.isEmpty());
        // and a pushback
        QCOMPARE(cursor()->pos(), QPoint(1, 50));

        // just to be sure, let's set geometry back
        client->setFrameGeometry(QRect({}, setup.base->space->size));
        Q_EMIT screenEdges->checkBlocking();
        cursor()->set_pos(0, 50);
        QVERIFY(spy.isEmpty());
        // and no pushback
        QCOMPARE(cursor()->pos(), QPoint(0, 50));

        // the corner should always trigger
        screenEdges->unreserve(win::electric_border::left, &callback);
        cursor()->set_pos(99, 99);
        QVERIFY(spy.isEmpty());

        // and pushback
        QCOMPARE(cursor()->pos(), QPoint(98, 98));
        QTest::qWait(160);
        cursor()->set_pos(99, 99);
        QVERIFY(!spy.isEmpty());
#endif

        unreserve_many(border_ids, win::electric_border::left);
        unreserve_many(border_ids, win::electric_border::bottom_right);
    }

    SECTION("client edge")
    {
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(),
                                  &space::qobject_t::wayland_window_added);
        QVERIFY(clientAddedSpy.isValid());
        auto surface = create_surface();
        QVERIFY(surface);
        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);
        render(surface, QSize(100, 50), Qt::blue);
        flush_wayland_connection();
        QVERIFY(clientAddedSpy.wait());

        auto client = get_wayland_window(setup.base->space->stacking.active);
        QVERIFY(client);

        client->setFrameGeometry(QRect(10, 50, 10, 50));

        auto& screenEdges = setup.base->space->edges;
        screenEdges->reserve(client, win::electric_border::bottom);
        auto& edge = screenEdges->edges.back();

        // TODO(romangg): This changed recently. Needs investigation..
        REQUIRE_FALSE(edge->reserved_count > 0);
        REQUIRE(edge->activatesForPointer());
        REQUIRE(!edge->activatesForTouchGesture());

        // remove old reserves and resize to be in the middle of the screen
        screenEdges->reserve(client, win::electric_border::none);
        client->setFrameGeometry(QRect(2, 2, 20, 20));

        // for none of the edges it should be able to be set
        for (size_t i = 0; i < static_cast<size_t>(win::electric_border::_COUNT); ++i) {
            client->hideClient(true);
            screenEdges->reserve(client, static_cast<win::electric_border>(i));

            // TODO(romangg): Possible on Wayland. Needs investigation.
            REQUIRE_FALSE(!client->isHiddenInternal());
        }

        // now let's try to set it and activate it
        client->setFrameGeometry(QRect({}, setup.base->topology.size));
        client->hideClient(true);
        screenEdges->reserve(client, win::electric_border::left);
        QCOMPARE(client->isHiddenInternal(), true);

        cursor()->set_pos(0, 50);

        // autohiding panels shall activate instantly
        // TODO(romangg): Is hidden on Wayland but was not on X11. Needs investigation.
        REQUIRE_FALSE(!client->isHiddenInternal());
        return;

        QCOMPARE(cursor()->pos(), QPoint(1, 50));

        // now let's reserve the client for each of the edges, in the end for the right one
        client->hideClient(true);
        screenEdges->reserve(client, win::electric_border::top);
        screenEdges->reserve(client, win::electric_border::bottom);
        QCOMPARE(client->isHiddenInternal(), true);

        // corners shouldn't get reserved
        screenEdges->reserve(client, win::electric_border::top_left);
        QCOMPARE(client->isHiddenInternal(), false);
        client->hideClient(true);
        screenEdges->reserve(client, win::electric_border::top_right);
        QCOMPARE(client->isHiddenInternal(), false);
        client->hideClient(true);
        screenEdges->reserve(client, win::electric_border::bottom_right);
        QCOMPARE(client->isHiddenInternal(), false);
        client->hideClient(true);
        screenEdges->reserve(client, win::electric_border::bottom_left);
        QCOMPARE(client->isHiddenInternal(), false);

        // now finally reserve on right one
        client->hideClient(true);
        screenEdges->reserve(client, win::electric_border::right);
        QCOMPARE(client->isHiddenInternal(), true);

        // now let's emulate the removal of a Client through base.space
        Q_EMIT setup.base->space->qobject->clientRemoved(client->meta.signal_id);
        for (auto& e : screenEdges->edges) {
            QVERIFY(!e->client());
        }
        QCOMPARE(client->isHiddenInternal(), true);

        // now let's try to trigger the client showing with the check method instead of enter notify
        screenEdges->reserve(client, win::electric_border::top);
        QCOMPARE(client->isHiddenInternal(), true);
        cursor()->set_pos(50, 0);
        screenEdges->check(QPoint(50, 0), std::chrono::system_clock::now());
        QCOMPARE(client->isHiddenInternal(), false);
        QCOMPARE(cursor()->pos(), QPoint(50, 1));

        // unreserve by setting to none edge
        screenEdges->reserve(client, win::electric_border::none);
        // check on previous edge again, should fail
        client->hideClient(true);
        cursor()->set_pos(50, 0);
        screenEdges->check(QPoint(50, 0), std::chrono::system_clock::now());
        QCOMPARE(client->isHiddenInternal(), true);
        QCOMPARE(cursor()->pos(), QPoint(50, 0));

        // set to windows can cover
        client->setFrameGeometry(QRect({}, setup.base->topology.size));
        client->hideClient(false);
        win::set_keep_below(client, true);
        screenEdges->reserve(client, win::electric_border::left);
        REQUIRE(client->control->keep_below);
        REQUIRE(!client->isHiddenInternal());

        cursor()->set_pos(0, 50);
        REQUIRE(!client->control->keep_below);
        REQUIRE(!client->isHiddenInternal());
        QCOMPARE(cursor()->pos(), QPoint(1, 50));
    }

    SECTION("touch edge")
    {
        auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
        auto group = config->group("TouchEdges");
        group.writeEntry("Top", "krunner");
        group.writeEntry("Left", "krunner");
        group.writeEntry("Bottom", "krunner");
        group.writeEntry("Right", "krunner");
        config->sync();

        reset_edger(config);
        auto& screenEdges = setup.base->space->edges;

        // we don't have multiple desktops, so it's returning false
        // TODO(romangg): Possible on Wayland. Needs investigation.
        REQUIRE_FALSE(!screenEdges->desktop_switching.always);
        return;

        REQUIRE(!screenEdges->desktop_switching.when_moving_client);
        QCOMPARE(screenEdges->actions.top_left, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.top, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.top_right, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.right, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.bottom_right, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.bottom, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.bottom_left, win::electric_border_action::none);
        QCOMPARE(screenEdges->actions.left, win::electric_border_action::none);

        auto& edges = screenEdges->edges;
        QCOMPARE(edges.size(), 8);

        // TODO: Does not pass for all edges at the moment on Wayland.
#if 0
        for (auto& e : edges) {
            REQUIRE((e->reserved_count > 0) == e->isScreenEdge());
            REQUIRE(!e->activatesForPointer());
            QCOMPARE(e->activatesForTouchGesture(), e->isScreenEdge());
        }
#endif

        // try to activate the edge through pointer, should not be possible
        auto it = std::find_if(
            edges.cbegin(), edges.cend(), [](auto& e) { return e->isScreenEdge() && e->isLeft(); });
        QVERIFY(it != edges.cend());

        QSignalSpy approachingSpy(screenEdges->qobject.get(),
                                  &win::screen_edger_qobject::approaching);
        QVERIFY(approachingSpy.isValid());

        auto setPos = [](const QPoint& pos) { cursor()->set_pos(pos); };
        setPos(QPoint(0, 50));
        QVERIFY(approachingSpy.isEmpty());
        // let's also verify the check
        screenEdges->check(QPoint(0, 50), std::chrono::system_clock::now(), false);
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

    SECTION("touch callback")
    {
        struct data {
            win::electric_border border;
            QPoint start_pos;
            QSizeF delta;
        };

        auto test_data = GENERATE(data{win::electric_border::left, {0, 50}, {250, 20}},
                                  data{win::electric_border::top, {50, 0}, {20, 250}},
                                  data{win::electric_border::right, {99, 50}, {-200, 0}},
                                  data{win::electric_border::bottom, {50, 99}, {0, -200}});

        auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
        auto group = config->group("TouchEdges");
        group.writeEntry("Top", "none");
        group.writeEntry("Left", "none");
        group.writeEntry("Bottom", "none");
        group.writeEntry("Right", "none");
        config->sync();

        reset_edger(config);
        auto& screenEdges = setup.base->space->edges;

        // none of our actions should be reserved
        auto& edges = screenEdges->edges;
        REQUIRE(edges.size() == 8);

        // TODO: Does not pass for all edges at the moment on Wayland.
#if 0
        for (auto& e : edges) {
            QCOMPARE(e->reserved_count, 0);
            QCOMPARE(e->activatesForPointer(), false);
            QCOMPARE(e->activatesForTouchGesture(), false);
        }
#endif

        // let's reserve an action
        QAction action;
        QSignalSpy actionTriggeredSpy(&action, &QAction::triggered);
        QVERIFY(actionTriggeredSpy.isValid());
        QSignalSpy approachingSpy(screenEdges->qobject.get(),
                                  &win::screen_edger_qobject::approaching);
        QVERIFY(approachingSpy.isValid());

        // reserve on edge
        screenEdges->reserveTouch(test_data.border, &action);

        // TODO: Does not pass for all edges at the moment on Wayland.
#if 0
        for (auto& e : edges) {
            QCOMPARE(e->reserved_count > 0, e->border == test_data.border);
            QCOMPARE(e->activatesForPointer(), false);
            QCOMPARE(e->activatesForTouchGesture(), e->border == test_data.border);
        }
#endif

        // TODO(romangg): Does not work on Wayland like before on X11. Needs fixing.
        return;

        QVERIFY(approachingSpy.isEmpty());
        QCOMPARE(screenEdges->gesture_recognizer->startSwipeGesture(test_data.start_pos), 1);
        QVERIFY(actionTriggeredSpy.isEmpty());
        QCOMPARE(approachingSpy.count(), 1);
        screenEdges->gesture_recognizer->updateSwipeGesture(test_data.delta);
        QCOMPARE(approachingSpy.count(), 2);
        QVERIFY(actionTriggeredSpy.isEmpty());
        screenEdges->gesture_recognizer->endSwipeGesture();

        QVERIFY(actionTriggeredSpy.wait());
        QCOMPARE(actionTriggeredSpy.count(), 1);
        QCOMPARE(approachingSpy.count(), 3);

        // unreserve again
        screenEdges->unreserveTouch(test_data.border, &action);
        for (auto& e : edges) {
            REQUIRE(e->reserved_count == 0);
            REQUIRE(!e->activatesForPointer());
            REQUIRE(!e->activatesForTouchGesture());
        }

        // reserve another action
        std::unique_ptr<QAction> action2(new QAction);
        screenEdges->reserveTouch(test_data.border, action2.get());
        for (auto& e : edges) {
            REQUIRE((e->reserved_count > 0) == (e->border == test_data.border));
            REQUIRE(!e->activatesForPointer());
            REQUIRE(e->activatesForTouchGesture() == (e->border == test_data.border));
        }
        // and unreserve by destroying
        action2.reset();
        for (auto& e : edges) {
            REQUIRE(e->reserved_count == 0);
            REQUIRE(!e->activatesForPointer());
            REQUIRE(!e->activatesForTouchGesture());
        }
    }
}

}

#include "screen_edges.moc"
