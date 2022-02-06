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
#include "screens.h"
#include "win/meta.h"
#include "win/stacking_order.h"
#include "win/wayland/window.h"
#include "win/x11/stacking_tree.h"
#include "win/x11/window.h"
#include "workspace.h"

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

void X11ClientTest::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();
    qRegisterMetaType<KWin::win::x11::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());
    kwinApp()->setConfig(KSharedConfig::openConfig(QString(), KConfig::SimpleConfig));

    Test::app()->start();
    QVERIFY(startup_spy.wait());
    QVERIFY(render::compositor::self());
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
                      rootWindow(),
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
    NETWinInfo winInfo(c.get(), w, rootWindow(), NET::Properties(), NET::Properties2());
    QFETCH(QByteArray, originalTitle);
    winInfo.setName(originalTitle);
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    // we should get a client for it
    QSignalSpy windowCreatedSpy(workspace(), &Workspace::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client);
    QCOMPARE(client->xcb_window(), w);
    QFETCH(QByteArray, expectedTitle);
    QCOMPARE(win::caption(client), QString::fromUtf8(expectedTitle));

    // and destroy the window again
    xcb_unmap_window(c.get(), w);
    xcb_flush(c.get());

    QSignalSpy windowClosedSpy(client, &win::x11::window::windowClosed);
    QVERIFY(windowClosedSpy.isValid());
    QVERIFY(windowClosedSpy.wait());
    xcb_destroy_window(c.get(), w);
    c.reset();
}

void X11ClientTest::testFullscreenLayerWithActiveWaylandWindow()
{
    // this test verifies that an X11 fullscreen window does not stay in the active layer
    // when a Wayland window is active, see BUG: 375759
    QCOMPARE(Test::app()->base.screens.count(), 1);

    // first create an X11 window
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    const QRect windowGeometry(0, 0, 100, 200);
    xcb_window_t w = xcb_generate_id(c.get());
    xcb_create_window(c.get(),
                      XCB_COPY_FROM_PARENT,
                      w,
                      rootWindow(),
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
    QSignalSpy windowCreatedSpy(workspace(), &Workspace::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client);
    QCOMPARE(client->xcb_window(), w);
    QVERIFY(!client->control->fullscreen());
    QVERIFY(client->control->active());
    QCOMPARE(client->layer(), win::layer::normal);

    workspace()->slotWindowFullScreen();
    QVERIFY(client->control->fullscreen());
    QCOMPARE(client->layer(), win::layer::active);
    QCOMPARE(workspace()->stacking_order->sorted().back(), client);

    // now let's open a Wayland window
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    auto waylandClient = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(waylandClient);
    QVERIFY(waylandClient->control->active());
    QCOMPARE(waylandClient->layer(), win::layer::normal);
    QCOMPARE(workspace()->stacking_order->sorted().back(), waylandClient);
    QCOMPARE(workspace()->x_stacking_tree->as_list().back(), waylandClient);
    QCOMPARE(client->layer(), win::layer::normal);

    // now activate fullscreen again
    workspace()->activateClient(client);
    QTRY_VERIFY(client->control->active());
    QCOMPARE(client->layer(), win::layer::active);
    QCOMPARE(workspace()->stacking_order->sorted().back(), client);
    QCOMPARE(workspace()->x_stacking_tree->as_list().back(), client);

    // activate wayland window again
    workspace()->activateClient(waylandClient);
    QTRY_VERIFY(waylandClient->control->active());
    QCOMPARE(workspace()->stacking_order->sorted().back(), waylandClient);
    QCOMPARE(workspace()->x_stacking_tree->as_list().back(), waylandClient);

    // back to x window
    workspace()->activateClient(client);
    QTRY_VERIFY(client->control->active());
    // remove fullscreen
    QVERIFY(client->control->fullscreen());
    workspace()->slotWindowFullScreen();
    QVERIFY(!client->control->fullscreen());
    // and fullscreen again
    workspace()->slotWindowFullScreen();
    QVERIFY(client->control->fullscreen());
    QCOMPARE(workspace()->stacking_order->sorted().back(), client);
    QCOMPARE(workspace()->x_stacking_tree->as_list().back(), client);

    // activate wayland window again
    workspace()->activateClient(waylandClient);
    QTRY_VERIFY(waylandClient->control->active());
    QCOMPARE(workspace()->stacking_order->sorted().back(), waylandClient);
    QCOMPARE(workspace()->x_stacking_tree->as_list().back(), waylandClient);

    // back to X11 window
    workspace()->activateClient(client);
    QTRY_VERIFY(client->control->active());

    // remove fullscreen
    QVERIFY(client->control->fullscreen());
    workspace()->slotWindowFullScreen();
    QVERIFY(!client->control->fullscreen());

    // Wait a moment for the X11 client to catch up.
    // TODO(romangg): can we listen to a signal client-side?
    QTest::qWait(200);

    // and fullscreen through X API
    NETWinInfo info(c.get(), w, kwinApp()->x11RootWindow(), NET::Properties(), NET::Properties2());
    info.setState(NET::FullScreen, NET::FullScreen);
    NETRootInfo rootInfo(c.get(), NET::Properties());
    rootInfo.setActiveWindow(w, NET::FromApplication, XCB_CURRENT_TIME, XCB_WINDOW_NONE);

    QSignalSpy fullscreen_spy(client, &win::x11::window::fullScreenChanged);
    QVERIFY(fullscreen_spy.isValid());

    xcb_flush(c.get());

    QVERIFY(fullscreen_spy.wait());
    QTRY_VERIFY(client->control->fullscreen());
    QCOMPARE(workspace()->stacking_order->sorted().back(), client);
    QCOMPARE(workspace()->x_stacking_tree->as_list().back(), client);

    // activate wayland window again
    workspace()->activateClient(waylandClient);
    QTRY_VERIFY(waylandClient->control->active());
    QCOMPARE(workspace()->stacking_order->sorted().back(), waylandClient);
    QCOMPARE(workspace()->x_stacking_tree->as_list().back(), waylandClient);
    QCOMPARE(client->layer(), win::layer::normal);

    // close the window
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(waylandClient));
    QTRY_VERIFY(client->control->active());
    QCOMPARE(client->layer(), win::layer::active);

    // and destroy the window again
    xcb_unmap_window(c.get(), w);
    xcb_flush(c.get());
}

void X11ClientTest::testFocusInWithWaylandLastActiveWindow()
{
    // this test verifies that Workspace::allowClientActivation does not crash if last client was a
    // Wayland client

    // create an X11 window
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));
    const QRect windowGeometry(0, 0, 100, 200);
    xcb_window_t w = xcb_generate_id(c.get());
    xcb_create_window(c.get(),
                      XCB_COPY_FROM_PARENT,
                      w,
                      rootWindow(),
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
    QSignalSpy windowCreatedSpy(workspace(), &Workspace::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client);
    QCOMPARE(client->xcb_window(), w);
    QVERIFY(client->control->active());

    // create Wayland window
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    auto waylandClient = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(waylandClient);
    QVERIFY(waylandClient->control->active());
    // activate no window
    workspace()->setActiveClient(nullptr);
    QVERIFY(!waylandClient->control->active());
    QVERIFY(!workspace()->activeClient());
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
    QTRY_VERIFY(client->control->active());

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
                      rootWindow(),
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
    QSignalSpy windowCreatedSpy(workspace(), &Workspace::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client);
    QCOMPARE(client->xcb_window(), w);
    QVERIFY(client->control->active());
    QCOMPARE(client->xcb_window(), w);
    QCOMPARE(client->internalId().isNull(), false);
    const auto uuid = client->internalId();
    QUuid deletedUuid;
    QCOMPARE(deletedUuid.isNull(), true);

    connect(client, &win::x11::window::windowClosed, this, [&deletedUuid](Toplevel*, Toplevel* d) {
        deletedUuid = d->internalId();
    });

    NETRootInfo rootInfo(c.get(), NET::WMAllProperties);
    QCOMPARE(rootInfo.activeWindow(), client->xcb_window());

    // activate a wayland window
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    auto waylandClient = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(waylandClient);
    QVERIFY(waylandClient->control->active());
    xcb_flush(kwinApp()->x11Connection());

    NETRootInfo rootInfo2(c.get(), NET::WMAllProperties);
    QCOMPARE(rootInfo2.activeWindow(), 0u);

    // back to X11 client
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(waylandClient));

    QTRY_VERIFY(client->control->active());
    NETRootInfo rootInfo3(c.get(), NET::WMAllProperties);
    QCOMPARE(rootInfo3.activeWindow(), client->xcb_window());

    // and destroy the window again
    xcb_unmap_window(c.get(), w);
    xcb_flush(c.get());
    QSignalSpy windowClosedSpy(client, &win::x11::window::windowClosed);
    QVERIFY(windowClosedSpy.isValid());
    QVERIFY(windowClosedSpy.wait());

    QCOMPARE(deletedUuid.isNull(), false);
    QCOMPARE(deletedUuid, uuid);
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
                      rootWindow(),
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
    NETWinInfo info(c.get(), w, kwinApp()->x11RootWindow(), NET::Properties(), NET::Properties2());
    info.setName("foo");
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    // we should get a client for it
    QSignalSpy windowCreatedSpy(workspace(), &Workspace::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client);
    QCOMPARE(client->xcb_window(), w);
    QCOMPARE(win::caption(client), QStringLiteral("foo"));

    QSignalSpy captionChangedSpy(client, &win::x11::window::captionChanged);
    QVERIFY(captionChangedSpy.isValid());
    info.setName("bar");
    xcb_flush(c.get());
    QVERIFY(captionChangedSpy.wait());
    QCOMPARE(win::caption(client), QStringLiteral("bar"));

    // and destroy the window again
    QSignalSpy windowClosedSpy(client, &win::x11::window::windowClosed);
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
    QSignalSpy clientAddedSpy(workspace(), &Workspace::clientAdded);
    QVERIFY(clientAddedSpy.isValid());

    QProcess glxgears;
    glxgears.setProgram(QStringLiteral("glxgears"));
    glxgears.start();
    QVERIFY(glxgears.waitForStarted());

    QVERIFY(clientAddedSpy.wait());
    QCOMPARE(clientAddedSpy.count(), 1);
    QCOMPARE(workspace()->allClientList().size(), 1);
    auto glxgearsClient = workspace()->allClientList().front();
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
                      rootWindow(),
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
    NETWinInfo info(c.get(), w, kwinApp()->x11RootWindow(), NET::Properties(), NET::Properties2());
    info.setName("foo");
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    QSignalSpy windowCreatedSpy(workspace(), &Workspace::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client);
    QCOMPARE(client->xcb_window(), w);
    QCOMPARE(win::caption(client), QStringLiteral("foo"));

    // create second window with same caption
    xcb_window_t w2 = xcb_generate_id(c.get());
    xcb_create_window(c.get(),
                      XCB_COPY_FROM_PARENT,
                      w2,
                      rootWindow(),
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
        c.get(), w2, kwinApp()->x11RootWindow(), NET::Properties(), NET::Properties2());
    info2.setName("foo");
    info2.setIconName("foo");
    xcb_map_window(c.get(), w2);
    xcb_flush(c.get());

    windowCreatedSpy.clear();
    QVERIFY(windowCreatedSpy.wait());
    auto client2 = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client2);
    QCOMPARE(client2->xcb_window(), w2);
    QCOMPARE(win::caption(client2), QStringLiteral("foo <2>\u200E"));
    NETWinInfo info3(kwinApp()->x11Connection(),
                     w2,
                     kwinApp()->x11RootWindow(),
                     NET::WMVisibleName | NET::WMVisibleIconName,
                     NET::Properties2());
    QCOMPARE(QByteArray(info3.visibleName()), QByteArrayLiteral("foo <2>\u200E"));
    QCOMPARE(QByteArray(info3.visibleIconName()), QByteArrayLiteral("foo <2>\u200E"));

    QSignalSpy captionChangedSpy(client2, &win::x11::window::captionChanged);
    QVERIFY(captionChangedSpy.isValid());

    NETWinInfo info4(
        c.get(), w2, kwinApp()->x11RootWindow(), NET::Properties(), NET::Properties2());
    info4.setName("foobar");
    info4.setIconName("foobar");
    xcb_map_window(c.get(), w2);
    xcb_flush(c.get());

    QVERIFY(captionChangedSpy.wait());
    QCOMPARE(win::caption(client2), QStringLiteral("foobar"));
    NETWinInfo info5(kwinApp()->x11Connection(),
                     w2,
                     kwinApp()->x11RootWindow(),
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
                      rootWindow(),
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
                        workspace()->atoms->wm_client_leader,
                        XCB_ATOM_WINDOW,
                        32,
                        1,
                        &w);
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    QSignalSpy windowCreatedSpy(workspace(), &Workspace::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client);
    QCOMPARE(client->xcb_window(), w);
    QCOMPARE(client->control->active(), true);

    QCOMPARE(client->control->fullscreen(), false);
    QCOMPARE(client->layer(), win::layer::normal);
    workspace()->slotWindowFullScreen();
    QCOMPARE(client->control->fullscreen(), true);
    QCOMPARE(client->layer(), win::layer::active);

    // now let's create a second window
    windowCreatedSpy.clear();
    xcb_window_t w2 = xcb_generate_id(c.get());
    xcb_create_window(c.get(),
                      XCB_COPY_FROM_PARENT,
                      w2,
                      rootWindow(),
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
                        workspace()->atoms->wm_client_leader,
                        XCB_ATOM_WINDOW,
                        32,
                        1,
                        &w);
    xcb_map_window(c.get(), w2);
    xcb_flush(c.get());

    QVERIFY(windowCreatedSpy.wait());
    auto client2 = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client2);
    QVERIFY(client != client2);
    QCOMPARE(client2->xcb_window(), w2);
    QCOMPARE(client2->control->active(), true);
    QCOMPARE(client2->group(), client->group());
    // first client should be moved back to normal layer
    QCOMPARE(client->control->active(), false);
    QCOMPARE(client->control->fullscreen(), true);
    QCOMPARE(client->layer(), win::layer::normal);

    // activating the fullscreen window again, should move it to active layer
    workspace()->activateClient(client);
    QTRY_COMPARE(client->layer(), win::layer::active);
}

void X11ClientTest::testActivateFocusedWindow()
{
    // The window manager may call XSetInputFocus() on a window that already has focus, in which
    // case no FocusIn event will be generated and the window won't be marked as active. This test
    // verifies that we handle that subtle case properly.

    QSKIP("Focus is not restored properly when the active client is about to be unmapped");

    auto connection = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(connection.get()));

    QSignalSpy windowCreatedSpy(workspace(), &Workspace::clientAdded);
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
                      rootWindow(),
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
                        workspace()->atoms->wm_client_leader,
                        XCB_ATOM_WINDOW,
                        32,
                        1,
                        &window1);
    xcb_map_window(connection.get(), window1);
    xcb_flush(connection.get());
    QVERIFY(windowCreatedSpy.wait());
    auto client1 = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client1);
    QCOMPARE(client1->xcb_window(), window1);
    QCOMPARE(client1->control->active(), true);

    // Create the second test window.
    const xcb_window_t window2 = xcb_generate_id(connection.get());
    xcb_create_window(connection.get(),
                      XCB_COPY_FROM_PARENT,
                      window2,
                      rootWindow(),
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
                        workspace()->atoms->wm_client_leader,
                        XCB_ATOM_WINDOW,
                        32,
                        1,
                        &window2);
    xcb_map_window(connection.get(), window2);
    xcb_flush(connection.get());
    QVERIFY(windowCreatedSpy.wait());
    auto client2 = windowCreatedSpy.last().first().value<win::x11::window*>();
    QVERIFY(client2);
    QCOMPARE(client2->xcb_window(), window2);
    QCOMPARE(client2->control->active(), true);

    // When the second test window is destroyed, the window manager will attempt to activate the
    // next client in the focus chain, which is the first window.
    xcb_set_input_focus(connection.get(), XCB_INPUT_FOCUS_POINTER_ROOT, window1, XCB_CURRENT_TIME);
    xcb_destroy_window(connection.get(), window2);
    xcb_flush(connection.get());
    QVERIFY(Test::wait_for_destroyed(client2));
    QVERIFY(client1->control->active());

    // Destroy the first test window.
    xcb_destroy_window(connection.get(), window1);
    xcb_flush(connection.get());
    QVERIFY(Test::wait_for_destroyed(client1));
}

}

WAYLANDTEST_MAIN(KWin::X11ClientTest)
#include "x11_client_test.moc"
