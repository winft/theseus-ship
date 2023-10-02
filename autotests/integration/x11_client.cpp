/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "base/x11/atoms.h"
#include "render/compositor.h"
#include "render/effect_loader.h"
#include "render/effects.h"
#include "win/activation.h"
#include "win/active_window.h"
#include "win/meta.h"
#include "win/space.h"
#include "win/stacking_order.h"
#include "win/wayland/window.h"
#include "win/x11/window.h"

#include <Wrapland/Client/surface.h>
#include <catch2/generators/catch_generators.hpp>
#include <xcb/xcb_icccm.h>

using namespace Wrapland::Client;

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

TEST_CASE("x11 window", "[win]")
{
    test::setup setup("x11-window", base::operation_mode::xwayland);
    setup.start();
    setup_wayland_connection();

    auto get_x11_window_from_id
        = [&](uint32_t id) { return get_x11_window(setup.base->space->windows_map.at(id)); };

    SECTION("trim caption")
    {
        // this test verifies that caption is properly trimmed

        struct data {
            std::string original_title;
            std::string expected_title;
        };

        auto test_data = GENERATE(
            data{"Was tun, wenn Schüler Autismus haben?\342\200\250\342\200\250\342\200\250 – "
                 "Marlies Hübner - Mozilla Firefox",
                 "Was tun, wenn Schüler Autismus haben? – Marlies Hübner - Mozilla Firefox"},
            data{"\bTesting non\302\255printable:\177, emoij:\360\237\230\203, "
                 "non-characters:\357\277\276",
                 "Testing nonprintable:, emoij:\360\237\230\203, non-characters:"});

        // create an xcb window
        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));
        const QRect windowGeometry(0, 0, 100, 200);
        xcb_window_t w = xcb_generate_id(c.get());
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w,
                          setup.base->x11_data.root_window,
                          windowGeometry.x(),
                          windowGeometry.y(),
                          windowGeometry.width(),
                          windowGeometry.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
        xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
        xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
        win::x11::net::win_info winInfo(c.get(),
                                        w,
                                        setup.base->x11_data.root_window,
                                        win::x11::net::Properties(),
                                        win::x11::net::Properties2());
        winInfo.setName(test_data.original_title.c_str());
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        // we should get a client for it
        QSignalSpy windowCreatedSpy(setup.base->space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
        QVERIFY(client);
        QCOMPARE(client->xcb_windows.client, w);
        QCOMPARE(win::caption(client), QString::fromStdString(test_data.expected_title));

        // and destroy the window again
        xcb_unmap_window(c.get(), w);
        xcb_flush(c.get());

        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());
        QVERIFY(windowClosedSpy.wait());
        xcb_destroy_window(c.get(), w);
        c.reset();
    }

    SECTION("fullscreen layer with active wayland window")
    {
        // this test verifies that an X11 fullscreen window does not stay in the active layer
        // when a Wayland window is active, see BUG: 375759
        QCOMPARE(setup.base->outputs.size(), 1);

        // first create an X11 window
        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));
        const QRect windowGeometry(0, 0, 100, 200);
        xcb_window_t w = xcb_generate_id(c.get());
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w,
                          setup.base->x11_data.root_window,
                          windowGeometry.x(),
                          windowGeometry.y(),
                          windowGeometry.width(),
                          windowGeometry.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
        xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
        xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        // we should get a client for it
        QSignalSpy windowCreatedSpy(setup.base->space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
        QVERIFY(client);
        QCOMPARE(client->xcb_windows.client, w);
        QVERIFY(!client->control->fullscreen);
        QVERIFY(client->control->active);
        QCOMPARE(win::get_layer(*client), win::layer::normal);

        win::active_window_set_fullscreen(*setup.base->space);
        QVERIFY(client->control->fullscreen);
        QCOMPARE(win::get_layer(*client), win::layer::active);
        QCOMPARE(get_x11_window(setup.base->space->stacking.order.stack.back()), client);

        // now let's open a Wayland window
        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        auto waylandClient = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(waylandClient);
        QVERIFY(waylandClient->control->active);
        QCOMPARE(win::get_layer(*waylandClient), win::layer::normal);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.order.stack.back()), waylandClient);
        QCOMPARE(get_wayland_window(win::render_stack(setup.base->space->stacking.order).back()),
                 waylandClient);
        QCOMPARE(win::get_layer(*client), win::layer::normal);

        // now activate fullscreen again
        win::activate_window(*setup.base->space, *client);
        QTRY_VERIFY(client->control->active);
        QCOMPARE(win::get_layer(*client), win::layer::active);
        QCOMPARE(get_x11_window(setup.base->space->stacking.order.stack.back()), client);
        QCOMPARE(get_x11_window(win::render_stack(setup.base->space->stacking.order).back()),
                 client);

        // activate wayland window again
        win::activate_window(*setup.base->space, *waylandClient);
        QTRY_VERIFY(waylandClient->control->active);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.order.stack.back()), waylandClient);
        QCOMPARE(get_wayland_window(win::render_stack(setup.base->space->stacking.order).back()),
                 waylandClient);

        // back to x window
        win::activate_window(*setup.base->space, *client);
        QTRY_VERIFY(client->control->active);
        // remove fullscreen
        QVERIFY(client->control->fullscreen);
        win::active_window_set_fullscreen(*setup.base->space);
        QVERIFY(!client->control->fullscreen);
        // and fullscreen again
        win::active_window_set_fullscreen(*setup.base->space);
        QVERIFY(client->control->fullscreen);
        QCOMPARE(get_x11_window(setup.base->space->stacking.order.stack.back()), client);
        QCOMPARE(get_x11_window(win::render_stack(setup.base->space->stacking.order).back()),
                 client);

        // activate wayland window again
        win::activate_window(*setup.base->space, *waylandClient);
        QTRY_VERIFY(waylandClient->control->active);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.order.stack.back()), waylandClient);
        QCOMPARE(get_wayland_window(win::render_stack(setup.base->space->stacking.order).back()),
                 waylandClient);

        // back to X11 window
        win::activate_window(*setup.base->space, *client);
        QTRY_VERIFY(client->control->active);

        // remove fullscreen
        QVERIFY(client->control->fullscreen);
        win::active_window_set_fullscreen(*setup.base->space);
        QVERIFY(!client->control->fullscreen);

        // Wait a moment for the X11 client to catch up.
        // TODO(romangg): can we listen to a signal client-side?
        QTest::qWait(200);

        // and fullscreen through X API
        win::x11::net::win_info info(c.get(),
                                     w,
                                     setup.base->x11_data.root_window,
                                     win::x11::net::Properties(),
                                     win::x11::net::Properties2());
        info.setState(win::x11::net::FullScreen, win::x11::net::FullScreen);
        win::x11::net::root_info rootInfo(c.get(), win::x11::net::Properties());
        rootInfo.setActiveWindow(
            w, win::x11::net::FromApplication, XCB_CURRENT_TIME, XCB_WINDOW_NONE);

        QSignalSpy fullscreen_spy(client->qobject.get(), &win::window_qobject::fullScreenChanged);
        QVERIFY(fullscreen_spy.isValid());

        xcb_flush(c.get());

        QVERIFY(fullscreen_spy.wait());
        QTRY_VERIFY(client->control->fullscreen);
        QCOMPARE(get_x11_window(setup.base->space->stacking.order.stack.back()), client);
        QCOMPARE(get_x11_window(win::render_stack(setup.base->space->stacking.order).back()),
                 client);

        // activate wayland window again
        win::activate_window(*setup.base->space, *waylandClient);
        QTRY_VERIFY(waylandClient->control->active);
        QCOMPARE(get_wayland_window(setup.base->space->stacking.order.stack.back()), waylandClient);
        QCOMPARE(get_wayland_window(win::render_stack(setup.base->space->stacking.order).back()),
                 waylandClient);
        QCOMPARE(win::get_layer(*client), win::layer::normal);

        // close the window
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(waylandClient));
        QTRY_VERIFY(client->control->active);
        QCOMPARE(win::get_layer(*client), win::layer::active);

        // and destroy the window again
        xcb_unmap_window(c.get(), w);
        xcb_flush(c.get());
    }

    SECTION("focus in with wayland last active window")
    {
        // this test verifies that win::space::allowClientActivation does not crash if last client
        // was a Wayland client

        // create an X11 window
        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));
        const QRect windowGeometry(0, 0, 100, 200);
        xcb_window_t w = xcb_generate_id(c.get());
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w,
                          setup.base->x11_data.root_window,
                          windowGeometry.x(),
                          windowGeometry.y(),
                          windowGeometry.width(),
                          windowGeometry.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
        xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
        xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        // we should get a client for it
        QSignalSpy windowCreatedSpy(setup.base->space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
        QVERIFY(client);
        QCOMPARE(client->xcb_windows.client, w);
        QVERIFY(client->control->active);

        // create Wayland window
        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        auto waylandClient = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(waylandClient);
        QVERIFY(waylandClient->control->active);

        // activate no window
        win::unset_active_window(*setup.base->space);
        QVERIFY(!waylandClient->control->active);
        QVERIFY(!setup.base->space->stacking.active);

        // and close Wayland window again
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(waylandClient));

        // and try to activate the x11 client through X11 api
        const auto cookie
            = xcb_set_input_focus_checked(c.get(), XCB_INPUT_FOCUS_NONE, w, XCB_CURRENT_TIME);
        auto error = xcb_request_check(c.get(), cookie);
        QVERIFY(!error);
        // this accesses last_active_client on trying to activate
        QTRY_VERIFY(client->control->active);

        // and destroy the window again
        xcb_unmap_window(c.get(), w);
        xcb_flush(c.get());
    }

    SECTION("x11 window id")
    {
        // create an X11 window
        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));
        const QRect windowGeometry(0, 0, 100, 200);
        xcb_window_t w = xcb_generate_id(c.get());
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w,
                          setup.base->x11_data.root_window,
                          windowGeometry.x(),
                          windowGeometry.y(),
                          windowGeometry.width(),
                          windowGeometry.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
        xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
        xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        // we should get a client for it
        QSignalSpy windowCreatedSpy(setup.base->space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
        QVERIFY(client);
        QCOMPARE(client->xcb_windows.client, w);
        QVERIFY(client->control->active);
        QCOMPARE(client->xcb_windows.client, w);
        QCOMPARE(client->meta.internal_id.isNull(), false);
        auto const uuid = client->meta.internal_id;
        QUuid deletedUuid;
        QCOMPARE(deletedUuid.isNull(), true);

        auto const& uuidConnection = QObject::connect(
            client->space.qobject.get(),
            &space::qobject_t::remnant_created,
            client->space.qobject.get(),
            [&setup, &deletedUuid](auto win_id) {
                std::visit(overload{[&](auto&& win) { deletedUuid = win->meta.internal_id; }},
                           setup.base->space->windows_map.at(win_id));
            });

        win::x11::net::root_info rootInfo(c.get(), win::x11::net::WMAllProperties);
        QCOMPARE(rootInfo.activeWindow(), client->xcb_windows.client);

        // activate a wayland window
        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        auto waylandClient = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(waylandClient);
        QVERIFY(waylandClient->control->active);
        xcb_flush(setup.base->x11_data.connection);

        win::x11::net::root_info rootInfo2(c.get(), win::x11::net::WMAllProperties);
        QCOMPARE(rootInfo2.activeWindow(), 0u);

        // back to X11 client
        shellSurface.reset();
        surface.reset();
        QVERIFY(wait_for_destroyed(waylandClient));

        QTRY_VERIFY(client->control->active);
        win::x11::net::root_info rootInfo3(c.get(), win::x11::net::WMAllProperties);
        QCOMPARE(rootInfo3.activeWindow(), client->xcb_windows.client);

        // and destroy the window again
        xcb_unmap_window(c.get(), w);
        xcb_flush(c.get());
        QSignalSpy windowClosedSpy(client->space.qobject.get(), &space::qobject_t::remnant_created);
        QVERIFY(windowClosedSpy.isValid());
        QVERIFY(windowClosedSpy.wait());

        QCOMPARE(deletedUuid.isNull(), false);
        QCOMPARE(deletedUuid, uuid);

        QObject::disconnect(uuidConnection);
    }

    SECTION("caption changes")
    {
        // verifies that caption is updated correctly when the X11 window updates it
        // BUG: 383444
        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));
        const QRect windowGeometry(0, 0, 100, 200);
        xcb_window_t w = xcb_generate_id(c.get());
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w,
                          setup.base->x11_data.root_window,
                          windowGeometry.x(),
                          windowGeometry.y(),
                          windowGeometry.width(),
                          windowGeometry.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
        xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
        xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
        win::x11::net::win_info info(c.get(),
                                     w,
                                     setup.base->x11_data.root_window,
                                     win::x11::net::Properties(),
                                     win::x11::net::Properties2());
        info.setName("foo");
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        // we should get a client for it
        QSignalSpy windowCreatedSpy(setup.base->space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
        QVERIFY(client);
        QCOMPARE(client->xcb_windows.client, w);
        QCOMPARE(win::caption(client), QStringLiteral("foo"));

        QSignalSpy captionChangedSpy(client->qobject.get(), &win::window_qobject::captionChanged);
        QVERIFY(captionChangedSpy.isValid());
        info.setName("bar");
        xcb_flush(c.get());
        QVERIFY(captionChangedSpy.wait());
        QCOMPARE(win::caption(client), QStringLiteral("bar"));

        // and destroy the window again
        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());
        xcb_unmap_window(c.get(), w);
        xcb_flush(c.get());
        QVERIFY(windowClosedSpy.wait());
        xcb_destroy_window(c.get(), w);
        c.reset();
    }

    SECTION("caption wm name")
    {
        // this test verifies that a caption set through WM_NAME is read correctly

        // open glxgears as that one only uses WM_NAME
        QSignalSpy clientAddedSpy(setup.base->space->qobject.get(), &space::qobject_t::clientAdded);
        QVERIFY(clientAddedSpy.isValid());

        QProcess glxgears;
        glxgears.setProgram(QStringLiteral("glxgears"));
        glxgears.start();
        QVERIFY(glxgears.waitForStarted());

        QVERIFY(clientAddedSpy.wait());
        QCOMPARE(clientAddedSpy.count(), 1);
        QCOMPARE(setup.base->space->windows.size(), 1);

        auto glxgearsClient = get_x11_window(setup.base->space->windows.front());
        QVERIFY(glxgearsClient);
        QCOMPARE(win::caption(glxgearsClient), QStringLiteral("glxgears"));

        glxgears.terminate();
        QVERIFY(glxgears.waitForFinished());
    }

    SECTION("caption multiple windows")
    {
        // BUG 384760
        // create first window
        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));
        const QRect windowGeometry(0, 0, 100, 200);
        xcb_window_t w = xcb_generate_id(c.get());
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w,
                          setup.base->x11_data.root_window,
                          windowGeometry.x(),
                          windowGeometry.y(),
                          windowGeometry.width(),
                          windowGeometry.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
        xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
        xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
        win::x11::net::win_info info(c.get(),
                                     w,
                                     setup.base->x11_data.root_window,
                                     win::x11::net::Properties(),
                                     win::x11::net::Properties2());
        info.setName("foo");
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        QSignalSpy windowCreatedSpy(setup.base->space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
        QVERIFY(client);
        QCOMPARE(client->xcb_windows.client, w);
        QCOMPARE(win::caption(client), QStringLiteral("foo"));

        // create second window with same caption
        xcb_window_t w2 = xcb_generate_id(c.get());
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w2,
                          setup.base->x11_data.root_window,
                          windowGeometry.x(),
                          windowGeometry.y(),
                          windowGeometry.width(),
                          windowGeometry.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_icccm_set_wm_normal_hints(c.get(), w2, &hints);
        win::x11::net::win_info info2(c.get(),
                                      w2,
                                      setup.base->x11_data.root_window,
                                      win::x11::net::Properties(),
                                      win::x11::net::Properties2());
        info2.setName("foo");
        info2.setIconName("foo");
        xcb_map_window(c.get(), w2);
        xcb_flush(c.get());

        windowCreatedSpy.clear();
        QVERIFY(windowCreatedSpy.wait());

        auto client2 = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
        QVERIFY(client2);
        QCOMPARE(client2->xcb_windows.client, w2);
        QCOMPARE(win::caption(client2), QStringLiteral("foo <2>\u200E"));
        win::x11::net::win_info info3(setup.base->x11_data.connection,
                                      w2,
                                      setup.base->x11_data.root_window,
                                      win::x11::net::WMVisibleName
                                          | win::x11::net::WMVisibleIconName,
                                      win::x11::net::Properties2());
        QCOMPARE(QByteArray(info3.visibleName()), QByteArrayLiteral("foo <2>\u200E"));
        QCOMPARE(QByteArray(info3.visibleIconName()), QByteArrayLiteral("foo <2>\u200E"));

        QSignalSpy captionChangedSpy(client2->qobject.get(), &win::window_qobject::captionChanged);
        QVERIFY(captionChangedSpy.isValid());

        win::x11::net::win_info info4(c.get(),
                                      w2,
                                      setup.base->x11_data.root_window,
                                      win::x11::net::Properties(),
                                      win::x11::net::Properties2());
        info4.setName("foobar");
        info4.setIconName("foobar");
        xcb_map_window(c.get(), w2);
        xcb_flush(c.get());

        QVERIFY(captionChangedSpy.wait());
        QCOMPARE(win::caption(client2), QStringLiteral("foobar"));
        win::x11::net::win_info info5(setup.base->x11_data.connection,
                                      w2,
                                      setup.base->x11_data.root_window,
                                      win::x11::net::WMVisibleName
                                          | win::x11::net::WMVisibleIconName,
                                      win::x11::net::Properties2());
        QCOMPARE(QByteArray(info5.visibleName()), QByteArray());
        QTRY_COMPARE(QByteArray(info5.visibleIconName()), QByteArray());
    }

    SECTION("fullscreen window groups")
    {
        // this test creates an X11 window and puts it to full screen
        // then a second window is created which is in the same window group
        // BUG: 388310

        auto c = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(c.get()));
        const QRect windowGeometry(0, 0, 100, 200);
        xcb_window_t w = xcb_generate_id(c.get());
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w,
                          setup.base->x11_data.root_window,
                          windowGeometry.x(),
                          windowGeometry.y(),
                          windowGeometry.width(),
                          windowGeometry.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
        xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());
        xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
        xcb_change_property(c.get(),
                            XCB_PROP_MODE_REPLACE,
                            w,
                            setup.base->space->atoms->wm_client_leader,
                            XCB_ATOM_WINDOW,
                            32,
                            1,
                            &w);
        xcb_map_window(c.get(), w);
        xcb_flush(c.get());

        QSignalSpy windowCreatedSpy(setup.base->space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());
        QVERIFY(windowCreatedSpy.wait());

        auto client = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
        QVERIFY(client);
        QCOMPARE(client->xcb_windows.client, w);
        QCOMPARE(client->control->active, true);

        QCOMPARE(client->control->fullscreen, false);
        QCOMPARE(win::get_layer(*client), win::layer::normal);
        win::active_window_set_fullscreen(*setup.base->space);
        QCOMPARE(client->control->fullscreen, true);
        QCOMPARE(win::get_layer(*client), win::layer::active);

        // now let's create a second window
        windowCreatedSpy.clear();
        xcb_window_t w2 = xcb_generate_id(c.get());
        xcb_create_window(c.get(),
                          XCB_COPY_FROM_PARENT,
                          w2,
                          setup.base->x11_data.root_window,
                          windowGeometry.x(),
                          windowGeometry.y(),
                          windowGeometry.width(),
                          windowGeometry.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_size_hints_t hints2;
        memset(&hints2, 0, sizeof(hints2));
        xcb_icccm_size_hints_set_position(&hints2, 1, windowGeometry.x(), windowGeometry.y());
        xcb_icccm_size_hints_set_size(&hints2, 1, windowGeometry.width(), windowGeometry.height());
        xcb_icccm_set_wm_normal_hints(c.get(), w2, &hints2);
        xcb_change_property(c.get(),
                            XCB_PROP_MODE_REPLACE,
                            w2,
                            setup.base->space->atoms->wm_client_leader,
                            XCB_ATOM_WINDOW,
                            32,
                            1,
                            &w);
        xcb_map_window(c.get(), w2);
        xcb_flush(c.get());

        QVERIFY(windowCreatedSpy.wait());

        auto client2 = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
        QVERIFY(client2);
        QVERIFY(client != client2);
        QCOMPARE(client2->xcb_windows.client, w2);
        QCOMPARE(client2->control->active, true);
        QCOMPARE(client2->group, client->group);
        // first client should be moved back to normal layer
        QCOMPARE(client->control->active, false);
        QCOMPARE(client->control->fullscreen, true);
        QCOMPARE(win::get_layer(*client), win::layer::normal);

        // activating the fullscreen window again, should move it to active layer
        win::activate_window(*setup.base->space, *client);
        QTRY_COMPARE(win::get_layer(*client), win::layer::active);
    }

    SECTION("activate focused window")
    {
        // The window manager may call XSetInputFocus() on a window that already has focus, in which
        // case no FocusIn event will be generated and the window won't be marked as active. This
        // test verifies that we handle that subtle case properly.

        QSKIP("Focus is not restored properly when the active client is about to be unmapped");

        auto connection = create_xcb_connection();
        QVERIFY(!xcb_connection_has_error(connection.get()));

        QSignalSpy windowCreatedSpy(setup.base->space->qobject.get(),
                                    &space::qobject_t::clientAdded);
        QVERIFY(windowCreatedSpy.isValid());

        const QRect windowGeometry(0, 0, 100, 200);
        xcb_size_hints_t hints;
        memset(&hints, 0, sizeof(hints));
        xcb_icccm_size_hints_set_position(&hints, 1, windowGeometry.x(), windowGeometry.y());
        xcb_icccm_size_hints_set_size(&hints, 1, windowGeometry.width(), windowGeometry.height());

        // Create the first test window.
        const xcb_window_t window1 = xcb_generate_id(connection.get());
        xcb_create_window(connection.get(),
                          XCB_COPY_FROM_PARENT,
                          window1,
                          setup.base->x11_data.root_window,
                          windowGeometry.x(),
                          windowGeometry.y(),
                          windowGeometry.width(),
                          windowGeometry.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_icccm_set_wm_normal_hints(connection.get(), window1, &hints);
        xcb_change_property(connection.get(),
                            XCB_PROP_MODE_REPLACE,
                            window1,
                            setup.base->space->atoms->wm_client_leader,
                            XCB_ATOM_WINDOW,
                            32,
                            1,
                            &window1);
        xcb_map_window(connection.get(), window1);
        xcb_flush(connection.get());
        QVERIFY(windowCreatedSpy.wait());

        auto client1 = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
        QVERIFY(client1);
        QCOMPARE(client1->xcb_windows.client, window1);
        QCOMPARE(client1->control->active, true);

        // Create the second test window.
        const xcb_window_t window2 = xcb_generate_id(connection.get());
        xcb_create_window(connection.get(),
                          XCB_COPY_FROM_PARENT,
                          window2,
                          setup.base->x11_data.root_window,
                          windowGeometry.x(),
                          windowGeometry.y(),
                          windowGeometry.width(),
                          windowGeometry.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          XCB_COPY_FROM_PARENT,
                          0,
                          nullptr);
        xcb_icccm_set_wm_normal_hints(connection.get(), window2, &hints);
        xcb_change_property(connection.get(),
                            XCB_PROP_MODE_REPLACE,
                            window2,
                            setup.base->space->atoms->wm_client_leader,
                            XCB_ATOM_WINDOW,
                            32,
                            1,
                            &window2);
        xcb_map_window(connection.get(), window2);
        xcb_flush(connection.get());
        QVERIFY(windowCreatedSpy.wait());

        auto client2 = get_x11_window_from_id(windowCreatedSpy.last().first().value<quint32>());
        QVERIFY(client2);
        QCOMPARE(client2->xcb_windows.client, window2);
        QCOMPARE(client2->control->active, true);

        // When the second test window is destroyed, the window manager will attempt to activate the
        // next client in the focus chain, which is the first window.
        xcb_set_input_focus(
            connection.get(), XCB_INPUT_FOCUS_POINTER_ROOT, window1, XCB_CURRENT_TIME);
        xcb_destroy_window(connection.get(), window2);
        xcb_flush(connection.get());
        QVERIFY(wait_for_destroyed(client2));
        QVERIFY(client1->control->active);

        // Destroy the first test window.
        xcb_destroy_window(connection.get(), window1);
        xcb_flush(connection.get());
        QVERIFY(wait_for_destroyed(client1));
    }
}

}
