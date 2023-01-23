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

#include <netwm.h>
#include <xcb/xcb_icccm.h>

using namespace Wrapland::Client;

namespace KWin
{

class X11ClientTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testTrimCaption_data();
    void testTrimCaption();
    void testFullscreenLayerWithActiveWaylandWindow();
    void testFocusInWithWaylandLastActiveWindow();
    void testX11WindowId();
    void testCaptionChanges();
    void testCaptionWmName();
    void testCaptionMultipleWindows();
    void testFullscreenWindowGroups();
    void testActivateFocusedWindow();
};

Test::space::x11_window* get_x11_window_from_id(uint32_t id)
{
    return Test::get_x11_window(Test::app()->base.space->windows_map.at(id));
}

void X11ClientTest::initTestCase()
{
    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    QVERIFY(startup_spy.wait());
    QVERIFY(Test::app()->base.render->compositor);
}

void X11ClientTest::init()
{
    Test::setup_wayland_connection();
}

void X11ClientTest::cleanup()
{
    Test::destroy_wayland_connection();
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

void X11ClientTest::testTrimCaption_data()
{
    QTest::addColumn<QByteArray>("originalTitle");
    QTest::addColumn<QByteArray>("expectedTitle");

    QTest::newRow("simplified")
        << QByteArrayLiteral(
               "Was tun, wenn Schüler Autismus haben?\342\200\250\342\200\250\342\200\250 – "
               "Marlies Hübner - Mozilla Firefox")
        << QByteArrayLiteral(
               "Was tun, wenn Schüler Autismus haben? – Marlies Hübner - Mozilla Firefox");

    QTest::newRow("with emojis") << QByteArrayLiteral(
        "\bTesting non\302\255printable:\177, emoij:\360\237\230\203, non-characters:\357\277\276")
                                 << QByteArrayLiteral(
                                        "Testing nonprintable:, emoij:\360\237\230\203, "
                                        "non-characters:");
}

void X11ClientTest::testTrimCaption()
{
    // this test verifies that caption is properly trimmed

    // create an xcb window
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    const QRect windowGeometry(0, 0, 100, 200);
    xcb_window_t w = xcb_generate_id(c.get());
    xcb_create_window(c.get(),
                      XCB_COPY_FROM_PARENT,
                      w,
                      Test::app()->base.x11_data.root_window,
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
    NETWinInfo winInfo(
        c.get(), w, Test::app()->base.x11_data.root_window, NET::Properties(), NET::Properties2());
    QFETCH(QByteArray, originalTitle);
    winInfo.setName(originalTitle);
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    // we should get a client for it
    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());

    auto client = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(client);
    QCOMPARE(client->xcb_windows.client, w);
    QFETCH(QByteArray, expectedTitle);
    QCOMPARE(win::caption(client), QString::fromUtf8(expectedTitle));

    // and destroy the window again
    xcb_unmap_window(c.get(), w);
    xcb_flush(c.get());

    QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
    QVERIFY(windowClosedSpy.isValid());
    QVERIFY(windowClosedSpy.wait());
    xcb_destroy_window(c.get(), w);
    c.reset();
}

void X11ClientTest::testFullscreenLayerWithActiveWaylandWindow()
{
    // this test verifies that an X11 fullscreen window does not stay in the active layer
    // when a Wayland window is active, see BUG: 375759
    QCOMPARE(Test::app()->base.get_outputs().size(), 1);

    // first create an X11 window
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    const QRect windowGeometry(0, 0, 100, 200);
    xcb_window_t w = xcb_generate_id(c.get());
    xcb_create_window(c.get(),
                      XCB_COPY_FROM_PARENT,
                      w,
                      Test::app()->base.x11_data.root_window,
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
    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());

    auto client = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(client);
    QCOMPARE(client->xcb_windows.client, w);
    QVERIFY(!client->control->fullscreen);
    QVERIFY(client->control->active);
    QCOMPARE(win::get_layer(*client), win::layer::normal);

    win::active_window_set_fullscreen(*Test::app()->base.space);
    QVERIFY(client->control->fullscreen);
    QCOMPARE(win::get_layer(*client), win::layer::active);
    QCOMPARE(Test::get_x11_window(Test::app()->base.space->stacking.order.stack.back()), client);

    // now let's open a Wayland window
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    auto waylandClient = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(waylandClient);
    QVERIFY(waylandClient->control->active);
    QCOMPARE(win::get_layer(*waylandClient), win::layer::normal);
    QCOMPARE(Test::get_wayland_window(Test::app()->base.space->stacking.order.stack.back()),
             waylandClient);
    QCOMPARE(
        Test::get_wayland_window(win::render_stack(Test::app()->base.space->stacking.order).back()),
        waylandClient);
    QCOMPARE(win::get_layer(*client), win::layer::normal);

    // now activate fullscreen again
    win::activate_window(*Test::app()->base.space, *client);
    QTRY_VERIFY(client->control->active);
    QCOMPARE(win::get_layer(*client), win::layer::active);
    QCOMPARE(Test::get_x11_window(Test::app()->base.space->stacking.order.stack.back()), client);
    QCOMPARE(
        Test::get_x11_window(win::render_stack(Test::app()->base.space->stacking.order).back()),
        client);

    // activate wayland window again
    win::activate_window(*Test::app()->base.space, *waylandClient);
    QTRY_VERIFY(waylandClient->control->active);
    QCOMPARE(Test::get_wayland_window(Test::app()->base.space->stacking.order.stack.back()),
             waylandClient);
    QCOMPARE(
        Test::get_wayland_window(win::render_stack(Test::app()->base.space->stacking.order).back()),
        waylandClient);

    // back to x window
    win::activate_window(*Test::app()->base.space, *client);
    QTRY_VERIFY(client->control->active);
    // remove fullscreen
    QVERIFY(client->control->fullscreen);
    win::active_window_set_fullscreen(*Test::app()->base.space);
    QVERIFY(!client->control->fullscreen);
    // and fullscreen again
    win::active_window_set_fullscreen(*Test::app()->base.space);
    QVERIFY(client->control->fullscreen);
    QCOMPARE(Test::get_x11_window(Test::app()->base.space->stacking.order.stack.back()), client);
    QCOMPARE(
        Test::get_x11_window(win::render_stack(Test::app()->base.space->stacking.order).back()),
        client);

    // activate wayland window again
    win::activate_window(*Test::app()->base.space, *waylandClient);
    QTRY_VERIFY(waylandClient->control->active);
    QCOMPARE(Test::get_wayland_window(Test::app()->base.space->stacking.order.stack.back()),
             waylandClient);
    QCOMPARE(
        Test::get_wayland_window(win::render_stack(Test::app()->base.space->stacking.order).back()),
        waylandClient);

    // back to X11 window
    win::activate_window(*Test::app()->base.space, *client);
    QTRY_VERIFY(client->control->active);

    // remove fullscreen
    QVERIFY(client->control->fullscreen);
    win::active_window_set_fullscreen(*Test::app()->base.space);
    QVERIFY(!client->control->fullscreen);

    // Wait a moment for the X11 client to catch up.
    // TODO(romangg): can we listen to a signal client-side?
    QTest::qWait(200);

    // and fullscreen through X API
    NETWinInfo info(
        c.get(), w, Test::app()->base.x11_data.root_window, NET::Properties(), NET::Properties2());
    info.setState(NET::FullScreen, NET::FullScreen);
    NETRootInfo rootInfo(c.get(), NET::Properties());
    rootInfo.setActiveWindow(w, NET::FromApplication, XCB_CURRENT_TIME, XCB_WINDOW_NONE);

    QSignalSpy fullscreen_spy(client->qobject.get(), &win::window_qobject::fullScreenChanged);
    QVERIFY(fullscreen_spy.isValid());

    xcb_flush(c.get());

    QVERIFY(fullscreen_spy.wait());
    QTRY_VERIFY(client->control->fullscreen);
    QCOMPARE(Test::get_x11_window(Test::app()->base.space->stacking.order.stack.back()), client);
    QCOMPARE(
        Test::get_x11_window(win::render_stack(Test::app()->base.space->stacking.order).back()),
        client);

    // activate wayland window again
    win::activate_window(*Test::app()->base.space, *waylandClient);
    QTRY_VERIFY(waylandClient->control->active);
    QCOMPARE(Test::get_wayland_window(Test::app()->base.space->stacking.order.stack.back()),
             waylandClient);
    QCOMPARE(
        Test::get_wayland_window(win::render_stack(Test::app()->base.space->stacking.order).back()),
        waylandClient);
    QCOMPARE(win::get_layer(*client), win::layer::normal);

    // close the window
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(waylandClient));
    QTRY_VERIFY(client->control->active);
    QCOMPARE(win::get_layer(*client), win::layer::active);

    // and destroy the window again
    xcb_unmap_window(c.get(), w);
    xcb_flush(c.get());
}

void X11ClientTest::testFocusInWithWaylandLastActiveWindow()
{
    // this test verifies that win::space::allowClientActivation does not crash if last client was a
    // Wayland client

    // create an X11 window
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    const QRect windowGeometry(0, 0, 100, 200);
    xcb_window_t w = xcb_generate_id(c.get());
    xcb_create_window(c.get(),
                      XCB_COPY_FROM_PARENT,
                      w,
                      Test::app()->base.x11_data.root_window,
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
    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());

    auto client = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(client);
    QCOMPARE(client->xcb_windows.client, w);
    QVERIFY(client->control->active);

    // create Wayland window
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    auto waylandClient = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(waylandClient);
    QVERIFY(waylandClient->control->active);

    // activate no window
    win::unset_active_window(*Test::app()->base.space);
    QVERIFY(!waylandClient->control->active);
    QVERIFY(!Test::app()->base.space->stacking.active);

    // and close Wayland window again
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(waylandClient));

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

void X11ClientTest::testX11WindowId()
{
    // create an X11 window
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    const QRect windowGeometry(0, 0, 100, 200);
    xcb_window_t w = xcb_generate_id(c.get());
    xcb_create_window(c.get(),
                      XCB_COPY_FROM_PARENT,
                      w,
                      Test::app()->base.x11_data.root_window,
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
    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
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

    const auto& uuidConnection
        = connect(client->space.qobject.get(),
                  &win::space::qobject_t::remnant_created,
                  this,
                  [&deletedUuid](auto win_id) {
                      std::visit(overload{[&](auto&& win) { deletedUuid = win->meta.internal_id; }},
                                 Test::app()->base.space->windows_map.at(win_id));
                  });

    NETRootInfo rootInfo(c.get(), NET::WMAllProperties);
    QCOMPARE(rootInfo.activeWindow(), client->xcb_windows.client);

    // activate a wayland window
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    auto waylandClient = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(waylandClient);
    QVERIFY(waylandClient->control->active);
    xcb_flush(kwinApp()->x11Connection());

    NETRootInfo rootInfo2(c.get(), NET::WMAllProperties);
    QCOMPARE(rootInfo2.activeWindow(), 0u);

    // back to X11 client
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(waylandClient));

    QTRY_VERIFY(client->control->active);
    NETRootInfo rootInfo3(c.get(), NET::WMAllProperties);
    QCOMPARE(rootInfo3.activeWindow(), client->xcb_windows.client);

    // and destroy the window again
    xcb_unmap_window(c.get(), w);
    xcb_flush(c.get());
    QSignalSpy windowClosedSpy(client->space.qobject.get(),
                               &win::space::qobject_t::remnant_created);
    QVERIFY(windowClosedSpy.isValid());
    QVERIFY(windowClosedSpy.wait());

    QCOMPARE(deletedUuid.isNull(), false);
    QCOMPARE(deletedUuid, uuid);

    disconnect(uuidConnection);
}

void X11ClientTest::testCaptionChanges()
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
                      Test::app()->base.x11_data.root_window,
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
    NETWinInfo info(
        c.get(), w, Test::app()->base.x11_data.root_window, NET::Properties(), NET::Properties2());
    info.setName("foo");
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    // we should get a client for it
    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
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

void X11ClientTest::testCaptionWmName()
{
    // this test verifies that a caption set through WM_NAME is read correctly

    // open glxgears as that one only uses WM_NAME
    QSignalSpy clientAddedSpy(Test::app()->base.space->qobject.get(),
                              &win::space::qobject_t::clientAdded);
    QVERIFY(clientAddedSpy.isValid());

    QProcess glxgears;
    glxgears.setProgram(QStringLiteral("glxgears"));
    glxgears.start();
    QVERIFY(glxgears.waitForStarted());

    QVERIFY(clientAddedSpy.wait());
    QCOMPARE(clientAddedSpy.count(), 1);
    QCOMPARE(Test::app()->base.space->windows.size(), 1);

    auto glxgearsClient = Test::get_x11_window(Test::app()->base.space->windows.front());
    QVERIFY(glxgearsClient);
    QCOMPARE(win::caption(glxgearsClient), QStringLiteral("glxgears"));

    glxgears.terminate();
    QVERIFY(glxgears.waitForFinished());
}

void X11ClientTest::testCaptionMultipleWindows()
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
                      Test::app()->base.x11_data.root_window,
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
    NETWinInfo info(
        c.get(), w, Test::app()->base.x11_data.root_window, NET::Properties(), NET::Properties2());
    info.setName("foo");
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
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
                      Test::app()->base.x11_data.root_window,
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
    NETWinInfo info2(
        c.get(), w2, Test::app()->base.x11_data.root_window, NET::Properties(), NET::Properties2());
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
    NETWinInfo info3(kwinApp()->x11Connection(),
                     w2,
                     Test::app()->base.x11_data.root_window,
                     NET::WMVisibleName | NET::WMVisibleIconName,
                     NET::Properties2());
    QCOMPARE(QByteArray(info3.visibleName()), QByteArrayLiteral("foo <2>\u200E"));
    QCOMPARE(QByteArray(info3.visibleIconName()), QByteArrayLiteral("foo <2>\u200E"));

    QSignalSpy captionChangedSpy(client2->qobject.get(), &win::window_qobject::captionChanged);
    QVERIFY(captionChangedSpy.isValid());

    NETWinInfo info4(
        c.get(), w2, Test::app()->base.x11_data.root_window, NET::Properties(), NET::Properties2());
    info4.setName("foobar");
    info4.setIconName("foobar");
    xcb_map_window(c.get(), w2);
    xcb_flush(c.get());

    QVERIFY(captionChangedSpy.wait());
    QCOMPARE(win::caption(client2), QStringLiteral("foobar"));
    NETWinInfo info5(kwinApp()->x11Connection(),
                     w2,
                     Test::app()->base.x11_data.root_window,
                     NET::WMVisibleName | NET::WMVisibleIconName,
                     NET::Properties2());
    QCOMPARE(QByteArray(info5.visibleName()), QByteArray());
    QTRY_COMPARE(QByteArray(info5.visibleIconName()), QByteArray());
}

void X11ClientTest::testFullscreenWindowGroups()
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
                      Test::app()->base.x11_data.root_window,
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
                        Test::app()->base.space->atoms->wm_client_leader,
                        XCB_ATOM_WINDOW,
                        32,
                        1,
                        &w);
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());

    auto client = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(client);
    QCOMPARE(client->xcb_windows.client, w);
    QCOMPARE(client->control->active, true);

    QCOMPARE(client->control->fullscreen, false);
    QCOMPARE(win::get_layer(*client), win::layer::normal);
    win::active_window_set_fullscreen(*Test::app()->base.space);
    QCOMPARE(client->control->fullscreen, true);
    QCOMPARE(win::get_layer(*client), win::layer::active);

    // now let's create a second window
    windowCreatedSpy.clear();
    xcb_window_t w2 = xcb_generate_id(c.get());
    xcb_create_window(c.get(),
                      XCB_COPY_FROM_PARENT,
                      w2,
                      Test::app()->base.x11_data.root_window,
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
                        Test::app()->base.space->atoms->wm_client_leader,
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
    win::activate_window(*Test::app()->base.space, *client);
    QTRY_COMPARE(win::get_layer(*client), win::layer::active);
}

void X11ClientTest::testActivateFocusedWindow()
{
    // The window manager may call XSetInputFocus() on a window that already has focus, in which
    // case no FocusIn event will be generated and the window won't be marked as active. This test
    // verifies that we handle that subtle case properly.

    QSKIP("Focus is not restored properly when the active client is about to be unmapped");

    auto connection = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(connection.get()));

    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
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
                      Test::app()->base.x11_data.root_window,
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
                        Test::app()->base.space->atoms->wm_client_leader,
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
                      Test::app()->base.x11_data.root_window,
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
                        Test::app()->base.space->atoms->wm_client_leader,
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
    xcb_set_input_focus(connection.get(), XCB_INPUT_FOCUS_POINTER_ROOT, window1, XCB_CURRENT_TIME);
    xcb_destroy_window(connection.get(), window2);
    xcb_flush(connection.get());
    QVERIFY(Test::wait_for_destroyed(client2));
    QVERIFY(client1->control->active);

    // Destroy the first test window.
    xcb_destroy_window(connection.get(), window1);
    xcb_flush(connection.get());
    QVERIFY(Test::wait_for_destroyed(client1));
}

}

WAYLANDTEST_MAIN(KWin::X11ClientTest)
#include "x11_client_test.moc"
