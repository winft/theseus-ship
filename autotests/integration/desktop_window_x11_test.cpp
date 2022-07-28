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
#include "base/x11/xcb/proto.h"
#include "input/cursor.h"
#include "win/deco.h"
#include "win/screen_edges.h"
#include "win/space.h"
#include "win/wayland/window.h"
#include "win/x11/window.h"

#include <netwm.h>
#include <xcb/xcb_icccm.h>

namespace KWin
{

class X11DesktopWindowTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testDesktopWindow();

private:
};

void X11DesktopWindowTest::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();
    qRegisterMetaType<KWin::win::x11::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.wait());
    Test::test_outputs_default();
}

void X11DesktopWindowTest::init()
{
    Test::app()->base.input->cursor->set_pos(QPoint(640, 512));
}

void X11DesktopWindowTest::cleanup()
{
}

void xcb_connection_deleter(xcb_connection_t* pointer)
{
    xcb_disconnect(pointer);
}

using xcb_connection_ptr = std::unique_ptr<xcb_connection_t, void (*)(xcb_connection_t*)>;

xcb_connection_ptr create_xcb_connection()
{
    return xcb_connection_ptr(xcb_connect(nullptr, nullptr), xcb_connection_deleter);
}

void X11DesktopWindowTest::testDesktopWindow()
{
    // this test creates a desktop window with an RGBA visual and verifies that it's only considered
    // as an RGB (opaque) window in KWin

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
        c.get(), XCB_COLORMAP_ALLOC_NONE, colormapId, rootWindow(), visualId);
    QVERIFY(!xcb_request_check(c.get(), cmCookie));

    const uint32_t values[] = {XCB_PIXMAP_NONE, defaultScreen()->black_pixel, colormapId};
    auto cookie
        = xcb_create_window_checked(c.get(),
                                    32,
                                    w,
                                    rootWindow(),
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
    NETWinInfo info(c.get(), w, rootWindow(), NET::WMAllProperties, NET::WM2AllProperties);
    info.setWindowType(NET::Desktop);
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    // verify through a geometry request that it's depth 32
    base::x11::xcb::geometry geo(w);
    QCOMPARE(geo->depth, uint8_t(32));

    // we should get a client for it
    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client);
    QCOMPARE(client->xcb_window, w);
    QVERIFY(!win::decoration(client));
    QCOMPARE(client->windowType(), NET::Desktop);
    QCOMPARE(client->frameGeometry(), windowGeometry);
    QVERIFY(win::is_desktop(client));
    QCOMPARE(client->bit_depth, 24);
    QVERIFY(!client->hasAlpha());

    // and destroy the window again
    xcb_unmap_window(c.get(), w);
    xcb_destroy_window(c.get(), w);
    xcb_flush(c.get());
    c.reset();

    QSignalSpy windowClosedSpy(client, &win::x11::window::closed);
    QVERIFY(windowClosedSpy.isValid());
    QVERIFY(windowClosedSpy.wait());
}

}

WAYLANDTEST_MAIN(KWin::X11DesktopWindowTest)
#include "desktop_window_x11_test.moc"
