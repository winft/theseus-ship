/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <KDecoration2/Decoration>
#include <linux/input.h>

namespace KWin::detail::test
{

TEST_CASE("no crash empty deco", "[win]")
{
    // This test verifies that resizing an X11 window to an invalid size does not result in crash on
    // unmap when the DecorationRenderer gets copied to the Deleted. There a repaint is scheduled
    // and the resulting texture is invalid if the window size is invalid.

    // this test needs to enforce OpenGL compositing to get into the crashy condition
    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));

    test::setup setup("no-crash-empty-deco", base::operation_mode::xwayland);
    setup.start();
    setup.set_outputs(2);
    test_outputs_default();

    auto& scene = setup.base->mod.render->scene;
    QVERIFY(scene);
    REQUIRE(scene->isOpenGl());

    cursor()->set_pos(QPoint(640, 512));

    // create an xcb window
    auto con = xcb_connection_create();
    QVERIFY(!xcb_connection_has_error(con.get()));

    xcb_window_t w = xcb_generate_id(con.get());
    xcb_create_window(con.get(),
                      XCB_COPY_FROM_PARENT,
                      w,
                      setup.base->x11_data.root_window,
                      0,
                      0,
                      10,
                      10,
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      XCB_COPY_FROM_PARENT,
                      0,
                      nullptr);
    xcb_map_window(con.get(), w);
    xcb_flush(con.get());

    // we should get a client for it
    QSignalSpy windowCreatedSpy(setup.base->mod.space->qobject.get(),
                                &space::qobject_t::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());

    auto win_id = windowCreatedSpy.first().first().value<quint32>();
    auto client = get_x11_window(setup.base->mod.space->windows_map.at(win_id));
    QVERIFY(client);
    QCOMPARE(client->xcb_windows.client, w);
    QVERIFY(win::decoration(client));

    // let's set a stupid geometry
    client->setFrameGeometry(QRect(0, 0, 0, 0));
    QCOMPARE(client->geo.frame, QRect(0, 0, 0, 0));

    // and destroy the window again
    xcb_unmap_window(con.get(), w);
    xcb_destroy_window(con.get(), w);
    xcb_flush(con.get());

    QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
    QVERIFY(windowClosedSpy.isValid());
    QVERIFY(windowClosedSpy.wait());
}

}
