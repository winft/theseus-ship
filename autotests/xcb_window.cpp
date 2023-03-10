/*
SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "integration/lib/setup.h"

#include "base/x11/xcb/proto.h"
#include "base/x11/xcb/window.h"

namespace KWin::detail::test
{

TEST_CASE("xcb window", "[unit],[win],[xwl]")
{
    test::setup setup("xcb-window", base::operation_mode::xwayland);
    setup.start();

    auto connection = setup.base->x11_data.connection;
    auto root_window = setup.base->x11_data.root_window;

    auto createWindow = [&]() {
        xcb_window_t w = xcb_generate_id(connection);
        uint32_t const values[] = {true};
        xcb_create_window(connection,
                          0,
                          w,
                          root_window,
                          0,
                          0,
                          10,
                          10,
                          0,
                          XCB_WINDOW_CLASS_INPUT_ONLY,
                          XCB_COPY_FROM_PARENT,
                          XCB_CW_OVERRIDE_REDIRECT,
                          values);
        return w;
    };

    SECTION("default ctor")
    {
        base::x11::xcb::window window;
        QCOMPARE(window.is_valid(), false);
        xcb_window_t wId = window;
        QCOMPARE(wId, XCB_WINDOW_NONE);

        xcb_window_t nativeWindow = createWindow();
        base::x11::xcb::window window2(connection, nativeWindow);
        QCOMPARE(window2.is_valid(), true);
        wId = window2;
        QCOMPARE(wId, nativeWindow);
    }

    SECTION("ctor")
    {
        const QRect geo(0, 0, 10, 10);
        const uint32_t values[] = {true};
        base::x11::xcb::window window(
            connection, root_window, geo, XCB_CW_OVERRIDE_REDIRECT, values);
        QCOMPARE(window.is_valid(), true);
        QVERIFY(window != XCB_WINDOW_NONE);
        base::x11::xcb::geometry windowGeometry(connection, window);
        QCOMPARE(windowGeometry.is_null(), false);
        QCOMPARE(windowGeometry.rect(), geo);
    }

    SECTION("class ctor")
    {
        const QRect geo(0, 0, 10, 10);
        const uint32_t values[] = {true};
        base::x11::xcb::window window(connection,
                                      root_window,
                                      geo,
                                      XCB_WINDOW_CLASS_INPUT_ONLY,
                                      XCB_CW_OVERRIDE_REDIRECT,
                                      values);
        QCOMPARE(window.is_valid(), true);
        QVERIFY(window != XCB_WINDOW_NONE);
        base::x11::xcb::geometry windowGeometry(connection, window);
        QCOMPARE(windowGeometry.is_null(), false);
        QCOMPARE(windowGeometry.rect(), geo);

        base::x11::xcb::window_attributes attribs(connection, window);
        QCOMPARE(attribs.is_null(), false);
        QVERIFY(attribs->_class == XCB_WINDOW_CLASS_INPUT_ONLY);
    }

    SECTION("create")
    {
        base::x11::xcb::window window;
        QCOMPARE(window.is_valid(), false);
        xcb_window_t wId = window;
        QCOMPARE(wId, XCB_WINDOW_NONE);

        const QRect geo(0, 0, 10, 10);
        const uint32_t values[] = {true};
        window.create(connection, root_window, geo, XCB_CW_OVERRIDE_REDIRECT, values);
        QCOMPARE(window.is_valid(), true);
        QVERIFY(window != XCB_WINDOW_NONE);
        // and reset again
        window.reset();
        QCOMPARE(window.is_valid(), false);
        QVERIFY(window == XCB_WINDOW_NONE);
    }

    SECTION("map unmap")
    {
        const QRect geo(0, 0, 10, 10);
        const uint32_t values[] = {true};
        base::x11::xcb::window window(connection,
                                      root_window,
                                      geo,
                                      XCB_WINDOW_CLASS_INPUT_ONLY,
                                      XCB_CW_OVERRIDE_REDIRECT,
                                      values);
        base::x11::xcb::window_attributes attribs(connection, window);
        QCOMPARE(attribs.is_null(), false);
        QVERIFY(attribs->map_state == XCB_MAP_STATE_UNMAPPED);

        window.map();
        base::x11::xcb::window_attributes attribs2(connection, window);
        QCOMPARE(attribs2.is_null(), false);
        QVERIFY(attribs2->map_state != XCB_MAP_STATE_UNMAPPED);

        window.unmap();
        base::x11::xcb::window_attributes attribs3(connection, window);
        QCOMPARE(attribs3.is_null(), false);
        QVERIFY(attribs3->map_state == XCB_MAP_STATE_UNMAPPED);

        // map, unmap shouldn't fail for an invalid window, it's just ignored
        window.reset();
        window.map();
        window.unmap();
    }

    SECTION("geometry")
    {
        const QRect geo(0, 0, 10, 10);
        const uint32_t values[] = {true};
        base::x11::xcb::window window(connection,
                                      root_window,
                                      geo,
                                      XCB_WINDOW_CLASS_INPUT_ONLY,
                                      XCB_CW_OVERRIDE_REDIRECT,
                                      values);
        base::x11::xcb::geometry windowGeometry(connection, window);
        QCOMPARE(windowGeometry.is_null(), false);
        QCOMPARE(windowGeometry.rect(), geo);

        const QRect geo2(10, 20, 100, 200);
        window.set_geometry(geo2);
        base::x11::xcb::geometry windowGeometry2(connection, window);
        QCOMPARE(windowGeometry2.is_null(), false);
        QCOMPARE(windowGeometry2.rect(), geo2);

        // setting a geometry on an invalid window should be ignored
        window.reset();
        window.set_geometry(geo2);
        base::x11::xcb::geometry windowGeometry3(connection, window);
        QCOMPARE(windowGeometry3.is_null(), true);
    }

    SECTION("destroy")
    {
        const QRect geo(0, 0, 10, 10);
        const uint32_t values[] = {true};
        base::x11::xcb::window window(
            connection, root_window, geo, XCB_CW_OVERRIDE_REDIRECT, values);
        QCOMPARE(window.is_valid(), true);
        xcb_window_t wId = window;

        window.create(connection, root_window, geo, XCB_CW_OVERRIDE_REDIRECT, values);
        // wId should now be invalid
        xcb_generic_error_t* error = nullptr;
        unique_cptr<xcb_get_window_attributes_reply_t> attribs(xcb_get_window_attributes_reply(
            connection, xcb_get_window_attributes(connection, wId), &error));
        QVERIFY(!attribs);
        QCOMPARE(error->error_code, uint8_t(3));
        QCOMPARE(error->resource_id, wId);
        free(error);

        // test the same for the dtor
        {
            base::x11::xcb::window scopedWindow(
                connection, root_window, geo, XCB_CW_OVERRIDE_REDIRECT, values);
            QVERIFY(scopedWindow.is_valid());
            wId = scopedWindow;
        }
        error = nullptr;
        unique_cptr<xcb_get_window_attributes_reply_t> attribs2(xcb_get_window_attributes_reply(
            connection, xcb_get_window_attributes(connection, wId), &error));
        QVERIFY(!attribs2);
        QCOMPARE(error->error_code, uint8_t(3));
        QCOMPARE(error->resource_id, wId);
        free(error);
    }

    SECTION("destroy not managed")
    {
        base::x11::xcb::window window;
        // just destroy the non-existing window
        window.reset();

        // now let's add a window
        window.reset(connection, createWindow(), false);
        xcb_window_t w = window;
        window.reset();
        base::x11::xcb::window_attributes attribs(connection, w);
        QVERIFY(attribs);
    }
}

}
