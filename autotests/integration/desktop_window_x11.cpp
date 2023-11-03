/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "base/x11/xcb/proto.h"
#include "input/cursor.h"
#include "win/deco.h"
#include "win/screen_edges.h"
#include "win/wayland/window.h"
#include "win/x11/window.h"

#include <xcb/xcb_icccm.h>

namespace KWin::detail::test
{

void xcb_connection_deleter(xcb_connection_t* pointer)
{
    xcb_disconnect(pointer);
}

using xcb_connection_ptr = std::unique_ptr<xcb_connection_t, void (*)(xcb_connection_t*)>;

xcb_connection_ptr create_xcb_connection()
{
    return xcb_connection_ptr(xcb_connect(nullptr, nullptr), xcb_connection_deleter);
}

TEST_CASE("x11 desktop window", "[xwl],[win]")
{
    // Creates a desktop window with an RGBA visual and verifies that it's only considered as an RGB
    // (opaque) window by us.

    test::setup setup("x11-desktop-window", base::operation_mode::xwayland);
    setup.start();
    setup.set_outputs(2);
    test_outputs_default();

    cursor()->set_pos(QPoint(640, 512));

    // create an xcb window
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));

    xcb_window_t w = xcb_generate_id(c.get());
    const QRect windowGeometry(0, 0, 1280, 1024);

    // helper to find the visual
    auto findDepth = [&c]() -> xcb_visualid_t {
        // find a visual with 32 depth
        const xcb_setup_t* setup = xcb_get_setup(c.get());

        for (auto screen = xcb_setup_roots_iterator(setup); screen.rem; xcb_screen_next(&screen)) {
            for (auto depth = xcb_screen_allowed_depths_iterator(screen.data); depth.rem;
                 xcb_depth_next(&depth)) {
                if (depth.data->depth != 32) {
                    continue;
                }
                const int len = xcb_depth_visuals_length(depth.data);
                const xcb_visualtype_t* visuals = xcb_depth_visuals(depth.data);

                for (int i = 0; i < len; i++) {
                    return visuals[0].visual_id;
                }
            }
        }
        return 0;
    };
    auto visualId = findDepth();
    auto colormapId = xcb_generate_id(c.get());
    auto cmCookie = xcb_create_colormap_checked(
        c.get(), XCB_COLORMAP_ALLOC_NONE, colormapId, setup.base->x11_data.root_window, visualId);
    QVERIFY(!xcb_request_check(c.get(), cmCookie));

    const uint32_t values[] = {XCB_PIXMAP_NONE,
                               base::x11::get_default_screen(setup.base->x11_data)->black_pixel,
                               colormapId};
    auto cookie
        = xcb_create_window_checked(c.get(),
                                    32,
                                    w,
                                    setup.base->x11_data.root_window,
                                    windowGeometry.x(),
                                    windowGeometry.y(),
                                    windowGeometry.width(),
                                    windowGeometry.height(),
                                    0,
                                    XCB_WINDOW_CLASS_INPUT_OUTPUT,
                                    visualId,
                                    XCB_CW_BACK_PIXMAP | XCB_CW_BORDER_PIXEL | XCB_CW_COLORMAP,
                                    values);
    QVERIFY(!xcb_request_check(c.get(), cookie));
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
    win::x11::net::win_info info(c.get(),
                                 w,
                                 setup.base->x11_data.root_window,
                                 win::x11::net::WMAllProperties,
                                 win::x11::net::WM2AllProperties);
    info.setWindowType(win::win_type::desktop);
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    // verify through a geometry request that it's depth 32
    base::x11::xcb::geometry geo(setup.base->x11_data.connection, w);
    QCOMPARE(geo->depth, uint8_t(32));

    // we should get a client for it
    QSignalSpy windowCreatedSpy(setup.base->space->qobject.get(), &space::qobject_t::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());

    auto client_id = windowCreatedSpy.first().first().value<quint32>();
    auto client = get_x11_window(setup.base->space->windows_map.at(client_id));
    QVERIFY(client);
    QCOMPARE(client->xcb_windows.client, w);
    QVERIFY(!win::decoration(client));
    QCOMPARE(client->windowType(), win::win_type::desktop);
    QCOMPARE(client->geo.frame, windowGeometry);
    QVERIFY(win::is_desktop(client));
    QCOMPARE(client->render_data.bit_depth, 24);
    QVERIFY(!win::has_alpha(*client));

    // and destroy the window again
    xcb_unmap_window(c.get(), w);
    xcb_destroy_window(c.get(), w);
    xcb_flush(c.get());
    c.reset();

    QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
    QVERIFY(windowClosedSpy.isValid());
    QVERIFY(windowClosedSpy.wait());
}

}
