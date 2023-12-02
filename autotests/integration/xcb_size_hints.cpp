/*
SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <catch2/generators/catch_generators.hpp>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>

namespace KWin::detail::test
{

TEST_CASE("xcb size hints", "[win],[xwl]")
{
    test::setup setup("xcb-size-hints", base::operation_mode::xwayland);
    setup.start();

    auto connection = setup.base->x11_data.connection;
    auto root_window = setup.base->x11_data.root_window;

    base::x11::xcb::window m_testWindow;
    uint32_t const values[] = {true};
    m_testWindow.create(connection,
                        root_window,
                        QRect(0, 0, 10, 10),
                        XCB_WINDOW_CLASS_INPUT_ONLY,
                        XCB_CW_OVERRIDE_REDIRECT,
                        values);
    QVERIFY(m_testWindow.is_valid());

    SECTION("size hints")
    {
        struct data {
            QPoint userPos;
            QSize userSize;
            QSize minSize;
            QSize maxSize;
            QSize resizeInc;
            QSize minAspect;
            QSize maxAspect;
            QSize baseSize;
            int32_t gravity;
            // read for SizeHints
            int32_t expectedFlags;
            int32_t expectedPad0;
            int32_t expectedPad1;
            int32_t expectedPad2;
            int32_t expectedPad3;
            int32_t expectedMinWidth;
            int32_t expectedMinHeight;
            int32_t expectedMaxWidth;
            int32_t expectedMaxHeight;
            int32_t expectedWidthInc;
            int32_t expectedHeightInc;
            int32_t expectedMinAspectNum;
            int32_t expectedMinAspectDen;
            int32_t expectedMaxAspectNum;
            int32_t expectedMaxAspectDen;
            int32_t expectedBaseWidth;
            int32_t expectedBaseHeight;
            // read for GeometryHints
            QSize expectedMinSize;
            QSize expectedMaxSize;
            QSize expectedResizeIncrements;
            QSize expectedMinAspect;
            QSize expectedMaxAspect;
            QSize expectedBaseSize;
            int32_t expectedGravity;
        };

        auto test_data = GENERATE(data{{1, 2},
                                       {},
                                       {},
                                       {},
                                       {},
                                       {},
                                       {},
                                       {},
                                       0,
                                       1,
                                       1,
                                       2,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       {0, 0},
                                       {INT_MAX, INT_MAX},
                                       {1, 1},
                                       {1, INT_MAX},
                                       {INT_MAX, 1},
                                       {0, 0},
                                       XCB_GRAVITY_NORTH_WEST},
                                  data{{},
                                       {1, 2},
                                       {},
                                       {},
                                       {},
                                       {},
                                       {},
                                       {},
                                       0,
                                       2,
                                       0,
                                       0,
                                       1,
                                       2,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       {0, 0},
                                       {INT_MAX, INT_MAX},
                                       {1, 1},
                                       {1, INT_MAX},
                                       {INT_MAX, 1},
                                       {0, 0},
                                       XCB_GRAVITY_NORTH_WEST},
                                  data{{},
                                       {},
                                       {1, 2},
                                       {},
                                       {},
                                       {},
                                       {},
                                       {},
                                       0,
                                       16,
                                       0,
                                       0,
                                       0,
                                       0,
                                       1,
                                       2,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       {1, 2},
                                       {INT_MAX, INT_MAX},
                                       {1, 1},
                                       {1, INT_MAX},
                                       {INT_MAX, 1},
                                       {0, 0},
                                       XCB_GRAVITY_NORTH_WEST},
                                  data{{},
                                       {},
                                       {},
                                       {1, 2},
                                       {},
                                       {},
                                       {},
                                       {},
                                       0,
                                       32,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       1,
                                       2,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       {0, 0},
                                       {1, 2},
                                       {1, 1},
                                       {1, INT_MAX},
                                       {INT_MAX, 1},
                                       {0, 0},
                                       XCB_GRAVITY_NORTH_WEST},
                                  data{{},
                                       {},
                                       {},
                                       {0, 0},
                                       {},
                                       {},
                                       {},
                                       {},
                                       0,
                                       32,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       {0, 0},
                                       {1, 1},
                                       {1, 1},
                                       {1, INT_MAX},
                                       {INT_MAX, 1},
                                       {0, 0},
                                       XCB_GRAVITY_NORTH_WEST},
                                  data{{},
                                       {},
                                       {1, 2},
                                       {3, 4},
                                       {},
                                       {},
                                       {},
                                       {},
                                       0,
                                       48,
                                       0,
                                       0,
                                       0,
                                       0,
                                       1,
                                       2,
                                       3,
                                       4,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       {1, 2},
                                       {3, 4},
                                       {1, 1},
                                       {1, INT_MAX},
                                       {INT_MAX, 1},
                                       {0, 0},
                                       XCB_GRAVITY_NORTH_WEST},
                                  data{{},
                                       {},
                                       {},
                                       {},
                                       {1, 2},
                                       {},
                                       {},
                                       {},
                                       0,
                                       64,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       1,
                                       2,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       {0, 0},
                                       {INT_MAX, INT_MAX},
                                       {1, 2},
                                       {1, INT_MAX},
                                       {INT_MAX, 1},
                                       {0, 0},
                                       XCB_GRAVITY_NORTH_WEST},
                                  data{{},
                                       {},
                                       {},
                                       {},
                                       {0, 0},
                                       {},
                                       {},
                                       {},
                                       0,
                                       64,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       {0, 0},
                                       {INT_MAX, INT_MAX},
                                       {1, 1},
                                       {1, INT_MAX},
                                       {INT_MAX, 1},
                                       {0, 0},
                                       XCB_GRAVITY_NORTH_WEST},
                                  data{{},
                                       {},
                                       {},
                                       {},
                                       {},
                                       {1, 2},
                                       {3, 4},
                                       {},
                                       0,
                                       128,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       1,
                                       2,
                                       3,
                                       4,
                                       0,
                                       0,
                                       {0, 0},
                                       {INT_MAX, INT_MAX},
                                       {1, 1},
                                       {1, 2},
                                       {3, 4},
                                       {0, 0},
                                       XCB_GRAVITY_NORTH_WEST},
                                  data{{},
                                       {},
                                       {},
                                       {},
                                       {},
                                       {1, 0},
                                       {3, 0},
                                       {},
                                       0,
                                       128,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       1,
                                       0,
                                       3,
                                       0,
                                       0,
                                       0,
                                       {0, 0},
                                       {INT_MAX, INT_MAX},
                                       {1, 1},
                                       {1, 1},
                                       {3, 1},
                                       {0, 0},
                                       XCB_GRAVITY_NORTH_WEST},
                                  data{{},
                                       {},
                                       {},
                                       {},
                                       {},
                                       {},
                                       {},
                                       {1, 2},
                                       0,
                                       256,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       1,
                                       2,
                                       {1, 2},
                                       {INT_MAX, INT_MAX},
                                       {1, 1},
                                       {1, INT_MAX},
                                       {INT_MAX, 1},
                                       {1, 2},
                                       XCB_GRAVITY_NORTH_WEST},
                                  data{{},
                                       {},
                                       {},
                                       {},
                                       {},
                                       {},
                                       {},
                                       {},
                                       XCB_GRAVITY_STATIC,
                                       512,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       0,
                                       {0, 0},
                                       {INT_MAX, INT_MAX},
                                       {1, 1},
                                       {1, INT_MAX},
                                       {INT_MAX, 1},
                                       {0, 0},
                                       XCB_GRAVITY_STATIC},
                                  data{{1, 2},   {3, 4},   {5, 6},
                                       {7, 8},   {9, 10},  {11, 12},
                                       {13, 14}, {15, 16}, 1,
                                       1011,     1,        2,
                                       3,        4,        5,
                                       6,        7,        8,
                                       9,        10,       11,
                                       12,       13,       14,
                                       15,       16,       {5, 6},
                                       {7, 8},   {9, 10},  {11, 12},
                                       {13, 14}, {15, 16}, XCB_GRAVITY_NORTH_WEST});

        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));

        if (!test_data.userPos.isNull()) {
            xcb_icccm_size_hints_set_position(
                &hints, 1, test_data.userPos.x(), test_data.userPos.y());
        }
        if (test_data.userSize.isValid()) {
            xcb_icccm_size_hints_set_size(
                &hints, 1, test_data.userSize.width(), test_data.userSize.height());
        }
        if (test_data.minSize.isValid()) {
            xcb_icccm_size_hints_set_min_size(
                &hints, test_data.minSize.width(), test_data.minSize.height());
        }
        if (test_data.maxSize.isValid()) {
            xcb_icccm_size_hints_set_max_size(
                &hints, test_data.maxSize.width(), test_data.maxSize.height());
        }
        if (test_data.resizeInc.isValid()) {
            xcb_icccm_size_hints_set_resize_inc(
                &hints, test_data.resizeInc.width(), test_data.resizeInc.height());
        }
        if (test_data.minAspect.isValid() && test_data.maxAspect.isValid()) {
            xcb_icccm_size_hints_set_aspect(&hints,
                                            test_data.minAspect.width(),
                                            test_data.minAspect.height(),
                                            test_data.maxAspect.width(),
                                            test_data.maxAspect.height());
        }
        if (test_data.baseSize.isValid()) {
            xcb_icccm_size_hints_set_base_size(
                &hints, test_data.baseSize.width(), test_data.baseSize.height());
        }
        if (test_data.gravity != 0) {
            xcb_icccm_size_hints_set_win_gravity(&hints, (xcb_gravity_t)test_data.gravity);
        }
        xcb_icccm_set_wm_normal_hints(connection, m_testWindow, &hints);
        xcb_flush(connection);

        base::x11::xcb::geometry_hints geoHints(connection);
        geoHints.init(m_testWindow);
        geoHints.read();
        REQUIRE(geoHints.has_aspect()
                == (test_data.minAspect.isValid() && test_data.maxAspect.isValid()));
        QCOMPARE(geoHints.has_base_size(), test_data.baseSize.isValid());
        QCOMPARE(geoHints.has_max_size(), test_data.maxSize.isValid());
        QCOMPARE(geoHints.has_min_size(), test_data.minSize.isValid());
        QCOMPARE(geoHints.has_position(), !test_data.userPos.isNull());
        QCOMPARE(geoHints.has_resize_increments(), test_data.resizeInc.isValid());
        QCOMPARE(geoHints.has_size(), test_data.userSize.isValid());
        REQUIRE(geoHints.has_window_gravity() == (test_data.gravity != 0));
        REQUIRE(geoHints.base_size() == test_data.expectedBaseSize);
        REQUIRE(geoHints.max_aspect() == test_data.expectedMaxAspect);
        REQUIRE(geoHints.max_size() == test_data.expectedMaxSize);
        REQUIRE(geoHints.min_aspect() == test_data.expectedMinAspect);
        REQUIRE(geoHints.min_size() == test_data.expectedMinSize);
        REQUIRE(geoHints.resize_increments() == test_data.expectedResizeIncrements);
        REQUIRE(qint32(geoHints.window_gravity()) == test_data.expectedGravity);

        auto sizeHints = geoHints.m_sizeHints;
        QVERIFY(sizeHints);
        REQUIRE(sizeHints->flags == test_data.expectedFlags);
        REQUIRE(sizeHints->pad[0] == test_data.expectedPad0);
        REQUIRE(sizeHints->pad[1] == test_data.expectedPad1);
        REQUIRE(sizeHints->pad[2] == test_data.expectedPad2);
        REQUIRE(sizeHints->pad[3] == test_data.expectedPad3);
        REQUIRE(sizeHints->minWidth == test_data.expectedMinWidth);
        REQUIRE(sizeHints->minHeight == test_data.expectedMinHeight);
        REQUIRE(sizeHints->maxWidth == test_data.expectedMaxWidth);
        REQUIRE(sizeHints->maxHeight == test_data.expectedMaxHeight);
        REQUIRE(sizeHints->widthInc == test_data.expectedWidthInc);
        REQUIRE(sizeHints->heightInc == test_data.expectedHeightInc);
        REQUIRE(sizeHints->minAspect[0] == test_data.expectedMinAspectNum);
        REQUIRE(sizeHints->minAspect[1] == test_data.expectedMinAspectDen);
        REQUIRE(sizeHints->maxAspect[0] == test_data.expectedMaxAspectNum);
        REQUIRE(sizeHints->maxAspect[1] == test_data.expectedMaxAspectDen);
        REQUIRE(sizeHints->baseWidth == test_data.expectedBaseWidth);
        REQUIRE(sizeHints->baseHeight == test_data.expectedBaseHeight);
        QCOMPARE(sizeHints->winGravity, test_data.gravity);

        // copy
        auto sizeHints2 = *sizeHints;
        REQUIRE(sizeHints2.flags == test_data.expectedFlags);
        REQUIRE(sizeHints2.pad[0] == test_data.expectedPad0);
        REQUIRE(sizeHints2.pad[1] == test_data.expectedPad1);
        REQUIRE(sizeHints2.pad[2] == test_data.expectedPad2);
        REQUIRE(sizeHints2.pad[3] == test_data.expectedPad3);
        REQUIRE(sizeHints2.minWidth == test_data.expectedMinWidth);
        REQUIRE(sizeHints2.minHeight == test_data.expectedMinHeight);
        REQUIRE(sizeHints2.maxWidth == test_data.expectedMaxWidth);
        REQUIRE(sizeHints2.maxHeight == test_data.expectedMaxHeight);
        REQUIRE(sizeHints2.widthInc == test_data.expectedWidthInc);
        REQUIRE(sizeHints2.heightInc == test_data.expectedHeightInc);
        REQUIRE(sizeHints2.minAspect[0] == test_data.expectedMinAspectNum);
        REQUIRE(sizeHints2.minAspect[1] == test_data.expectedMinAspectDen);
        REQUIRE(sizeHints2.maxAspect[0] == test_data.expectedMaxAspectNum);
        REQUIRE(sizeHints2.maxAspect[1] == test_data.expectedMaxAspectDen);
        REQUIRE(sizeHints2.baseWidth == test_data.expectedBaseWidth);
        REQUIRE(sizeHints2.baseHeight == test_data.expectedBaseHeight);
        QCOMPARE(sizeHints2.winGravity, test_data.gravity);
    }

    SECTION("size hints empty")
    {
        xcb_size_hints_t xcbHints;
        memset(&xcbHints, 0, sizeof(xcbHints));
        xcb_icccm_set_wm_normal_hints(connection, m_testWindow, &xcbHints);
        xcb_flush(connection);

        base::x11::xcb::geometry_hints hints(connection);
        hints.init(m_testWindow);
        hints.read();
        QVERIFY(!hints.has_aspect());
        QVERIFY(!hints.has_base_size());
        QVERIFY(!hints.has_max_size());
        QVERIFY(!hints.has_min_size());
        QVERIFY(!hints.has_position());
        QVERIFY(!hints.has_resize_increments());
        QVERIFY(!hints.has_size());
        QVERIFY(!hints.has_window_gravity());

        QCOMPARE(hints.base_size(), QSize(0, 0));
        QCOMPARE(hints.max_aspect(), QSize(INT_MAX, 1));
        QCOMPARE(hints.max_size(), QSize(INT_MAX, INT_MAX));
        QCOMPARE(hints.min_aspect(), QSize(1, INT_MAX));
        QCOMPARE(hints.min_size(), QSize(0, 0));
        QCOMPARE(hints.resize_increments(), QSize(1, 1));
        QCOMPARE(hints.window_gravity(), XCB_GRAVITY_NORTH_WEST);

        auto sizeHints = hints.m_sizeHints;
        QVERIFY(sizeHints);
        QCOMPARE(sizeHints->flags, 0);
        QCOMPARE(sizeHints->pad[0], 0);
        QCOMPARE(sizeHints->pad[1], 0);
        QCOMPARE(sizeHints->pad[2], 0);
        QCOMPARE(sizeHints->pad[3], 0);
        QCOMPARE(sizeHints->minWidth, 0);
        QCOMPARE(sizeHints->minHeight, 0);
        QCOMPARE(sizeHints->maxWidth, 0);
        QCOMPARE(sizeHints->maxHeight, 0);
        QCOMPARE(sizeHints->widthInc, 0);
        QCOMPARE(sizeHints->heightInc, 0);
        QCOMPARE(sizeHints->minAspect[0], 0);
        QCOMPARE(sizeHints->minAspect[1], 0);
        QCOMPARE(sizeHints->maxAspect[0], 0);
        QCOMPARE(sizeHints->maxAspect[1], 0);
        QCOMPARE(sizeHints->baseWidth, 0);
        QCOMPARE(sizeHints->baseHeight, 0);
        QCOMPARE(sizeHints->winGravity, 0);
    }

    SECTION("size hints not set")
    {
        base::x11::xcb::geometry_hints hints(connection);
        hints.init(m_testWindow);
        hints.read();
        QVERIFY(!hints.m_sizeHints);
        QVERIFY(!hints.has_aspect());
        QVERIFY(!hints.has_base_size());
        QVERIFY(!hints.has_max_size());
        QVERIFY(!hints.has_min_size());
        QVERIFY(!hints.has_position());
        QVERIFY(!hints.has_resize_increments());
        QVERIFY(!hints.has_size());
        QVERIFY(!hints.has_window_gravity());

        QCOMPARE(hints.base_size(), QSize(0, 0));
        QCOMPARE(hints.max_aspect(), QSize(INT_MAX, 1));
        QCOMPARE(hints.max_size(), QSize(INT_MAX, INT_MAX));
        QCOMPARE(hints.min_aspect(), QSize(1, INT_MAX));
        QCOMPARE(hints.min_size(), QSize(0, 0));
        QCOMPARE(hints.resize_increments(), QSize(1, 1));
        QCOMPARE(hints.window_gravity(), XCB_GRAVITY_NORTH_WEST);
    }

    SECTION("geometry hints before init")
    {
        base::x11::xcb::geometry_hints hints(connection);
        QVERIFY(!hints.has_aspect());
        QVERIFY(!hints.has_base_size());
        QVERIFY(!hints.has_max_size());
        QVERIFY(!hints.has_min_size());
        QVERIFY(!hints.has_position());
        QVERIFY(!hints.has_resize_increments());
        QVERIFY(!hints.has_size());
        QVERIFY(!hints.has_window_gravity());

        QCOMPARE(hints.base_size(), QSize(0, 0));
        QCOMPARE(hints.max_aspect(), QSize(INT_MAX, 1));
        QCOMPARE(hints.max_size(), QSize(INT_MAX, INT_MAX));
        QCOMPARE(hints.min_aspect(), QSize(1, INT_MAX));
        QCOMPARE(hints.min_size(), QSize(0, 0));
        QCOMPARE(hints.resize_increments(), QSize(1, 1));
        QCOMPARE(hints.window_gravity(), XCB_GRAVITY_NORTH_WEST);
    }

    SECTION("geometry hints before read")
    {
        xcb_size_hints_t xcbHints;
        memset(&xcbHints, 0, sizeof(xcbHints));
        xcb_icccm_size_hints_set_position(&xcbHints, 1, 1, 2);
        xcb_icccm_set_wm_normal_hints(connection, m_testWindow, &xcbHints);
        xcb_flush(connection);

        base::x11::xcb::geometry_hints hints(connection);
        hints.init(m_testWindow);
        QVERIFY(!hints.has_aspect());
        QVERIFY(!hints.has_base_size());
        QVERIFY(!hints.has_max_size());
        QVERIFY(!hints.has_min_size());
        QVERIFY(!hints.has_position());
        QVERIFY(!hints.has_resize_increments());
        QVERIFY(!hints.has_size());
        QVERIFY(!hints.has_window_gravity());

        QCOMPARE(hints.base_size(), QSize(0, 0));
        QCOMPARE(hints.max_aspect(), QSize(INT_MAX, 1));
        QCOMPARE(hints.max_size(), QSize(INT_MAX, INT_MAX));
        QCOMPARE(hints.min_aspect(), QSize(1, INT_MAX));
        QCOMPARE(hints.min_size(), QSize(0, 0));
        QCOMPARE(hints.resize_increments(), QSize(1, 1));
        QCOMPARE(hints.window_gravity(), XCB_GRAVITY_NORTH_WEST);
    }
}

}
