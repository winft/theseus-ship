/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "win/deco.h"
#include "win/screen_edges.h"
#include "win/wayland/space.h"
#include "win/x11/window.h"

#include <render/effect/interface/effects_handler.h>

#include <catch2/generators/catch_generators.hpp>
#include <xcb/xcb_icccm.h>

namespace KWin::detail::test
{

namespace
{

using xcb_connection_ptr = std::unique_ptr<xcb_connection_t, void (*)(xcb_connection_t*)>;

xcb_connection_ptr create_xcb_connection()
{
    return xcb_connection_ptr(xcb_connect(nullptr, nullptr), xcb_disconnect);
}

}

TEST_CASE("screen edge window show", "[win]")
{
    test::setup setup("screen-edge-window-show", base::operation_mode::xwayland);

    // set custom config which disable touch edge
    auto group = setup.base->config.main->group("TabBox");
    group.writeEntry(QStringLiteral("TouchBorderActivate"), "9");
    group.sync();

    setup.start();
    setup.set_outputs(2);
    test_outputs_default();
    cursor()->set_pos(QPoint(640, 512));

    SECTION("edge show hide x11")
    {
        // this test creates a window which borders the screen and sets the screenedge show hint
        // that should trigger a show of the window whenever the cursor is pushed against the screen
        // edge

        struct data {
            QRect window_geo;
            QRect resized_window_geo;
            uint32_t location;
            QPoint trigger_pos;
        };

        auto test_data = GENERATE(
            // bottom/left
            data{{50, 1004, 1180, 20}, {150, 1004, 1000, 20}, 2, {100, 1023}},
            // bottom/right
            data{{1330, 1004, 1180, 20}, {1410, 1004, 1000, 20}, 2, {1400, 1023}},
            // top/left
            data{{50, 0, 1180, 20}, {150, 0, 1000, 20}, 0, {100, 0}},
            // top/right
            data{{1330, 0, 1180, 20}, {1410, 0, 1000, 20}, 0, {1400, 0}},
            // left
            data{{0, 10, 20, 1000}, {0, 70, 20, 800}, 3, {0, 50}},
            // right
            data{{2540, 10, 20, 1000}, {2540, 70, 20, 800}, 1, {2559, 60}});

        // create the test window
        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));

        // atom for the screenedge show hide functionality
        base::x11::xcb::atom atom(
            QByteArrayLiteral("_KDE_NET_WM_SCREEN_EDGE_SHOW"), false, c.get());

        xcb_window_t w = xcb_generate_id(c.get());
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w,
                          setup.base->x11_data.root_window,
                          test_data.window_geo.x(),
                          test_data.window_geo.y(),
                          test_data.window_geo.width(),
                          test_data.window_geo.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_size_hints_set_position(
            &hints, 1, test_data.window_geo.x(), test_data.window_geo.y());
        xcb_icccm_size_hints_set_size(
            &hints, 1, test_data.window_geo.width(), test_data.window_geo.height());
        xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
        win::x11::net::win_info info(c.get(),
                                     w,
                                     setup.base->x11_data.root_window,
                                     win::x11::net::WMAllProperties,
                                     win::x11::net::WM2AllProperties);
        info.setWindowType(win::win_type::dock);
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        QSignalSpy windowCreatedSpy(setup.base->space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client_id = windowCreatedSpy.last().first().value<quint32>();
        auto client = get_x11_window(setup.base->space->windows_map.at(client_id));
        QVERIFY(client);

        // TODO(romangg): For unknown reason the windows of some data points have a deco.
        REQUIRE(!win::decoration(client));
        QCOMPARE(client->geo.frame, test_data.window_geo);
        QVERIFY(!client->hasStrut());
        QVERIFY(!client->isHiddenInternal());

        QSignalSpy effectsWindowAdded(effects, &EffectsHandler::windowAdded);
        QVERIFY(effectsWindowAdded.isValid());
        QVERIFY(effectsWindowAdded.wait());

        // now try to hide
        xcb_change_property(
            c.get(), XCB_PROP_MODE_REPLACE, w, atom, XCB_ATOM_CARDINAL, 32, 1, &test_data.location);
        xcb_flush(c.get());

        QSignalSpy effectsWindowHiddenSpy(effects, &EffectsHandler::windowHidden);
        QVERIFY(effectsWindowHiddenSpy.isValid());
        QSignalSpy clientHiddenSpy(client->qobject.get(), &win::window_qobject::windowHidden);
        QVERIFY(clientHiddenSpy.isValid());
        QVERIFY(clientHiddenSpy.wait());
        QVERIFY(client->isHiddenInternal());
        QCOMPARE(effectsWindowHiddenSpy.count(), 1);

        // now trigger the edge
        QSignalSpy effectsWindowShownSpy(effects, &EffectsHandler::windowShown);
        QVERIFY(effectsWindowShownSpy.isValid());
        cursor()->set_pos(test_data.trigger_pos);
        QVERIFY(!client->isHiddenInternal());
        QCOMPARE(effectsWindowShownSpy.count(), 1);

        // go into event loop to trigger xcb_flush
        QTest::qWait(1);

        // hide window again
        cursor()->set_pos(QPoint(640, 512));
        xcb_change_property(
            c.get(), XCB_PROP_MODE_REPLACE, w, atom, XCB_ATOM_CARDINAL, 32, 1, &test_data.location);
        xcb_flush(c.get());
        QVERIFY(clientHiddenSpy.wait());
        QVERIFY(client->isHiddenInternal());

        // resize while hidden
        client->setFrameGeometry(test_data.resized_window_geo);
        // test_data.trigger_pos shouldn't be valid anymore
        cursor()->set_pos(test_data.trigger_pos);
        QVERIFY(client->isHiddenInternal());

        // destroy window again
        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());
        xcb_unmap_window(c.get(), w);
        xcb_destroy_window(c.get(), w);
        xcb_flush(c.get());
        QVERIFY(windowClosedSpy.wait());
    }

    SECTION("edge show x11 touch")
    {
        // this test creates a window which borders the screen and sets the screenedge show hint
        // that should trigger a show of the window whenever the touch screen swipe gesture is
        // triggered

        struct data {
            QRect window_geo;
            uint32_t location;
            QPoint touch_down;
            QPoint target;
        };

        auto test_data = GENERATE(
            // bottom/left
            data{{50, 1004, 1180, 20}, 2, {100, 1023}, {100, 540}},
            // bottom/right
            data{{1330, 1004, 1180, 20}, 2, {1400, 1023}, {1400, 520}},
            // top/left
            data{{50, 0, 1180, 20}, 0, {100, 0}, {100, 350}},
            // top/right
            data{{1330, 0, 1180, 20}, 0, {1400, 0}, {1400, 400}},
            // left
            data{{0, 10, 20, 1000}, 3, {0, 50}, {400, 50}},
            // right
            data{{2540, 10, 20, 1000}, 1, {2559, 60}, {2200, 60}});

        // create the test window
        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));

        // atom for the screenedge show hide functionality
        base::x11::xcb::atom atom(
            QByteArrayLiteral("_KDE_NET_WM_SCREEN_EDGE_SHOW"), false, c.get());

        xcb_window_t w = xcb_generate_id(c.get());
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w,
                          setup.base->x11_data.root_window,
                          test_data.window_geo.x(),
                          test_data.window_geo.y(),
                          test_data.window_geo.width(),
                          test_data.window_geo.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_size_hints_set_position(
            &hints, 1, test_data.window_geo.x(), test_data.window_geo.y());
        xcb_icccm_size_hints_set_size(
            &hints, 1, test_data.window_geo.width(), test_data.window_geo.height());
        xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
        win::x11::net::win_info info(c.get(),
                                     w,
                                     setup.base->x11_data.root_window,
                                     win::x11::net::WMAllProperties,
                                     win::x11::net::WM2AllProperties);
        info.setWindowType(win::win_type::dock);
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        QSignalSpy windowCreatedSpy(setup.base->space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client_id = windowCreatedSpy.last().first().value<quint32>();
        auto client = get_x11_window(setup.base->space->windows_map.at(client_id));
        QVERIFY(client);
        QVERIFY(!win::decoration(client));
        QCOMPARE(client->geo.frame, test_data.window_geo);
        QVERIFY(!client->hasStrut());
        QVERIFY(!client->isHiddenInternal());

        QSignalSpy effectsWindowAdded(effects, &EffectsHandler::windowAdded);
        QVERIFY(effectsWindowAdded.isValid());
        QVERIFY(effectsWindowAdded.wait());

        // now try to hide
        xcb_change_property(
            c.get(), XCB_PROP_MODE_REPLACE, w, atom, XCB_ATOM_CARDINAL, 32, 1, &test_data.location);
        xcb_flush(c.get());

        QSignalSpy effectsWindowHiddenSpy(effects, &EffectsHandler::windowHidden);
        QVERIFY(effectsWindowHiddenSpy.isValid());
        QSignalSpy clientHiddenSpy(client->qobject.get(), &win::window_qobject::windowHidden);
        QVERIFY(clientHiddenSpy.isValid());
        QVERIFY(clientHiddenSpy.wait());
        QVERIFY(client->isHiddenInternal());
        QCOMPARE(effectsWindowHiddenSpy.count(), 1);

        // now trigger the edge
        QSignalSpy effectsWindowShownSpy(effects, &EffectsHandler::windowShown);
        QVERIFY(effectsWindowShownSpy.isValid());
        quint32 timestamp = 0;
        touch_down(0, test_data.touch_down, timestamp++);
        touch_motion(0, test_data.target, timestamp++);
        touch_up(0, timestamp++);
        QVERIFY(effectsWindowShownSpy.wait());
        QVERIFY(!client->isHiddenInternal());
        QCOMPARE(effectsWindowShownSpy.count(), 1);

        // destroy window again
        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());
        xcb_unmap_window(c.get(), w);
        xcb_destroy_window(c.get(), w);
        xcb_flush(c.get());
        QVERIFY(windowClosedSpy.wait());
    }
}

}
