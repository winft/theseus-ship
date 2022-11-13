/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>

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
#include "input/cursor.h"
#include "scripting/platform.h"
#include "scripting/script.h"
#include "win/deco/bridge.h"
#include "win/deco/settings.h"
#include "win/move.h"
#include "win/screen.h"
#include "win/wayland/space.h"
#include "win/window_operation.h"
#include "win/x11/window.h"

#include <KDecoration2/DecoratedClient>
#include <KDecoration2/Decoration>
#include <KDecoration2/DecorationSettings>

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>
#include <Wrapland/Client/xdgdecoration.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QTemporaryFile>
#include <QTextStream>

#include <netwm.h>
#include <xcb/xcb_icccm.h>

#include <linux/input.h>

Q_DECLARE_METATYPE(KWin::win::quicktiles)
Q_DECLARE_METATYPE(KWin::win::maximize_mode)

namespace KWin
{

class QuickTilingTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testQuickTiling_data();
    void testQuickTiling();
    void testQuickMaximizing_data();
    void testQuickMaximizing();
    void testQuickTilingKeyboardMove_data();
    void testQuickTilingKeyboardMove();
    void testQuickTilingPointerMove_data();
    void testQuickTilingPointerMove();
    void testQuickTilingTouchMove_data();
    void testQuickTilingTouchMove();
    void testX11QuickTiling_data();
    void testX11QuickTiling();
    void testX11QuickTilingAfterVertMaximize_data();
    void testX11QuickTilingAfterVertMaximize();
    void testShortcut_data();
    void testShortcut();
    void testScript_data();
    void testScript();

private:
    Wrapland::Client::ConnectionThread* m_connection = nullptr;
    Wrapland::Client::Compositor* m_compositor = nullptr;
};

Test::space::x11_window* get_x11_window_from_id(uint32_t id)
{
    return Test::get_x11_window(Test::app()->base.space->windows_map.at(id));
}

void QuickTilingTest::initTestCase()
{
    qRegisterMetaType<KWin::win::maximize_mode>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    // set custom config which disables the Outline
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    KConfigGroup group = config->group("Outline");
    group.writeEntry(QStringLiteral("QmlPath"), QString("/does/not/exist.qml"));
    group.sync();

    kwinApp()->setConfig(config);

    qputenv("XKB_DEFAULT_RULES", "evdev");

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.wait());
    Test::test_outputs_default();
}

void QuickTilingTest::init()
{
    Test::setup_wayland_connection(Test::global_selection::xdg_decoration);
    m_connection = Test::get_client().connection;
    m_compositor = Test::get_client().interfaces.compositor.get();

    Test::set_current_output(0);
}

void QuickTilingTest::cleanup()
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

Wrapland::Client::xdg_shell_states get_client_tiles(win::quicktiles tiles)
{
    Wrapland::Client::xdg_shell_states states;
    auto maximized{true};

    auto check_tile = [&](win::quicktiles tile, Wrapland::Client::xdg_shell_state state) {
        if (flags(tiles & tile)) {
            states |= state;
        } else {
            // When any tile is inactive, the state is not maximized.
            maximized = false;
        }
    };

    check_tile(win::quicktiles::left, Wrapland::Client::xdg_shell_state::tiled_left);
    check_tile(win::quicktiles::right, Wrapland::Client::xdg_shell_state::tiled_right);
    check_tile(win::quicktiles::top, Wrapland::Client::xdg_shell_state::tiled_top);
    check_tile(win::quicktiles::bottom, Wrapland::Client::xdg_shell_state::tiled_bottom);

    if (maximized) {
        states |= Wrapland::Client::xdg_shell_state::maximized;
    }
    return states;
}

void QuickTilingTest::testQuickTiling_data()
{
    QTest::addColumn<win::quicktiles>("mode");
    QTest::addColumn<QRect>("expectedGeometry");
    QTest::addColumn<QRect>("secondScreen");
    QTest::addColumn<win::quicktiles>("expectedModeAfterToggle");

    QTest::newRow("left") << win::quicktiles::left << QRect(0, 0, 640, 1024)
                          << QRect(1280, 0, 640, 1024) << win::quicktiles::right;
    QTest::newRow("top") << win::quicktiles::top << QRect(0, 0, 1280, 512)
                         << QRect(1280, 0, 1280, 512) << win::quicktiles::top;
    QTest::newRow("right") << win::quicktiles::right << QRect(640, 0, 640, 1024)
                           << QRect(1920, 0, 640, 1024) << win::quicktiles::none;
    QTest::newRow("bottom") << win::quicktiles::bottom << QRect(0, 512, 1280, 512)
                            << QRect(1280, 512, 1280, 512) << win::quicktiles::bottom;

    QTest::newRow("top left") << (win::quicktiles::left | win::quicktiles::top)
                              << QRect(0, 0, 640, 512) << QRect(1280, 0, 640, 512)
                              << (win::quicktiles::right | win::quicktiles::top);
    QTest::newRow("top right") << (win::quicktiles::right | win::quicktiles::top)
                               << QRect(640, 0, 640, 512) << QRect(1920, 0, 640, 512)
                               << win::quicktiles::none;
    QTest::newRow("bottom left") << (win::quicktiles::left | win::quicktiles::bottom)
                                 << QRect(0, 512, 640, 512) << QRect(1280, 512, 640, 512)
                                 << (win::quicktiles::right | win::quicktiles::bottom);
    QTest::newRow("bottom right") << (win::quicktiles::right | win::quicktiles::bottom)
                                  << QRect(640, 512, 640, 512) << QRect(1920, 512, 640, 512)
                                  << win::quicktiles::none;

    QTest::newRow("maximize") << win::quicktiles::maximize << QRect(0, 0, 1280, 1024)
                              << QRect(1280, 0, 1280, 1024) << win::quicktiles::none;
}

void QuickTilingTest::testQuickTiling()
{
    using namespace Wrapland::Client;

    auto surface = Test::create_surface();
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);

    // Map the client.
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(Test::get_wayland_window(Test::app()->base.space->stacking.active), c);
    QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
    QCOMPARE(c->control->quicktiling, win::quicktiles::none);

    QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    QSignalSpy quickTileChangedSpy(c->qobject.get(), &win::window_qobject::quicktiling_changed);
    QVERIFY(quickTileChangedSpy.isValid());
    QSignalSpy geometryChangedSpy(c->qobject.get(), &win::window_qobject::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());

    // We have to receive a configure event when the client becomes active.
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    QFETCH(win::quicktiles, mode);
    QFETCH(QRect, expectedGeometry);

    win::set_quicktile_mode(c, mode, true);
    QCOMPARE(quickTileChangedSpy.count(), 1);

    // at this point the geometry did not yet change
    QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));

    // but quick tile mode already changed
    QCOMPARE(c->control->quicktiling, mode);

    // but we got requested a new geometry
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 2);
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), expectedGeometry.size());
    QCOMPARE(configureRequestedSpy.last().at(1).value<xdg_shell_states>(),
             get_client_tiles(mode) | xdg_shell_state::activated);

    // attach a new image
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface, expectedGeometry.size(), Qt::red);

    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(geometryChangedSpy.count(), 1);
    QCOMPARE(c->geo.frame, expectedGeometry);

    // send window to other screen
    QCOMPARE(c->topo.central_output, Test::app()->base.outputs.at(0));

    auto output = base::get_output(Test::app()->base.outputs, 1);
    QVERIFY(output);
    win::send_to_screen(*Test::app()->base.space, c, *output);
    QCOMPARE(c->topo.central_output, Test::app()->base.outputs.at(1));

    // quick tile should not be changed
    QCOMPARE(c->control->quicktiling, mode);
    QTEST(c->geo.frame, "secondScreen");

    // now try to toggle again
    win::set_quicktile_mode(c, mode, true);
    QTEST(c->control->quicktiling, "expectedModeAfterToggle");
}

void QuickTilingTest::testQuickMaximizing_data()
{
    QTest::addColumn<win::quicktiles>("mode");

    QTest::newRow("maximize") << win::quicktiles::maximize;
    QTest::newRow("none") << win::quicktiles::none;
}

void QuickTilingTest::testQuickMaximizing()
{
    using namespace Wrapland::Client;

    auto surface = Test::create_surface();
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);

    // Map the client.
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(Test::get_wayland_window(Test::app()->base.space->stacking.active), c);
    QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
    QCOMPARE(c->control->quicktiling, win::quicktiles::none);
    QCOMPARE(c->maximizeMode(), win::maximize_mode::restore);

    // We have to receive a configure event upon becoming active.
    QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);
    QCOMPARE(configureRequestedSpy.last().at(1).value<xdg_shell_states>(),
             xdg_shell_state::activated);

    QSignalSpy quickTileChangedSpy(c->qobject.get(), &win::window_qobject::quicktiling_changed);
    QVERIFY(quickTileChangedSpy.isValid());
    QSignalSpy geometryChangedSpy(c->qobject.get(), &win::window_qobject::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());
    QSignalSpy maximizeChangedSpy(c->qobject.get(), &win::window_qobject::maximize_mode_changed);
    QVERIFY(maximizeChangedSpy.isValid());

    // Now quicktile-maximize.
    win::set_quicktile_mode(c, win::quicktiles::maximize, true);
    QCOMPARE(quickTileChangedSpy.count(), 1);

    // At this point the geometry did not yet change.
    QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
    // but quick tile mode already changed
    QCOMPARE(c->control->quicktiling, win::quicktiles::maximize);
    QCOMPARE(c->geo.restore.max, QRect(0, 0, 100, 50));

    // But we got requested a new geometry.
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 2);
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), QSize(1280, 1024));
    QCOMPARE(configureRequestedSpy.last().at(1).value<xdg_shell_states>(),
             get_client_tiles(win::quicktiles::maximize) | xdg_shell_state::activated);

    // Attach a new image.
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface, configureRequestedSpy.last().at(0).toSize(), Qt::red);

    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(geometryChangedSpy.count(), 1);
    QCOMPARE(c->geo.frame, QRect(0, 0, 1280, 1024));
    QCOMPARE(c->geo.restore.max, QRect(0, 0, 100, 50));

    // client is now set to maximised
    QCOMPARE(maximizeChangedSpy.count(), 1);
    QCOMPARE(maximizeChangedSpy.first().last().value<KWin::win::maximize_mode>(),
             win::maximize_mode::full);
    QCOMPARE(c->maximizeMode(), win::maximize_mode::full);

    // go back to quick tile none
    QFETCH(win::quicktiles, mode);
    win::set_quicktile_mode(c, mode, true);
    QCOMPARE(c->control->quicktiling, win::quicktiles::none);
    QCOMPARE(quickTileChangedSpy.count(), 2);

    // geometry not yet changed
    QCOMPARE(c->geo.frame, QRect(0, 0, 1280, 1024));
    QCOMPARE(c->geo.restore.max, QRect());

    // we got requested a new geometry
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 3);
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), QSize(100, 50));
    QCOMPARE(configureRequestedSpy.last().at(1).value<xdg_shell_states>(),
             xdg_shell_state::activated);

    // render again
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface, QSize(100, 50), Qt::yellow);

    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(geometryChangedSpy.count(), 2);
    QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
    QCOMPARE(maximizeChangedSpy.count(), 2);
    QCOMPARE(maximizeChangedSpy.last().last().value<KWin::win::maximize_mode>(),
             win::maximize_mode::restore);
}

void QuickTilingTest::testQuickTilingKeyboardMove_data()
{
    QTest::addColumn<QPoint>("targetPos");
    QTest::addColumn<win::quicktiles>("expectedMode");

    QTest::newRow("topRight") << QPoint(2559, 24)
                              << (win::quicktiles::top | win::quicktiles::right);
    QTest::newRow("right") << QPoint(2559, 512) << win::quicktiles::right;
    QTest::newRow("bottomRight") << QPoint(2559, 1023)
                                 << (win::quicktiles::bottom | win::quicktiles::right);
    QTest::newRow("bottomLeft") << QPoint(0, 1023)
                                << (win::quicktiles::bottom | win::quicktiles::left);
    QTest::newRow("Left") << QPoint(0, 512) << win::quicktiles::left;
    QTest::newRow("topLeft") << QPoint(0, 24) << (win::quicktiles::top | win::quicktiles::left);
}

void QuickTilingTest::testQuickTilingKeyboardMove()
{
    using namespace Wrapland::Client;

    auto surface = Test::create_surface();
    QVERIFY(surface);

    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);
    QSignalSpy sizeChangeSpy(shellSurface.get(), &XdgShellToplevel::sizeChanged);
    QVERIFY(sizeChangeSpy.isValid());
    // let's render
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

    QVERIFY(c);
    QCOMPARE(Test::get_wayland_window(Test::app()->base.space->stacking.active), c);
    QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
    QCOMPARE(c->control->quicktiling, win::quicktiles::none);
    QCOMPARE(c->maximizeMode(), win::maximize_mode::restore);

    QSignalSpy quickTileChangedSpy(c->qobject.get(), &win::window_qobject::quicktiling_changed);
    QVERIFY(quickTileChangedSpy.isValid());

    win::perform_window_operation(c, base::options_qobject::UnrestrictedMoveOp);
    QCOMPARE(c, Test::get_wayland_window(Test::app()->base.space->move_resize_window));
    QCOMPARE(Test::cursor()->pos(), QPoint(49, 24));

    QFETCH(QPoint, targetPos);
    quint32 timestamp = 1;
    Test::keyboard_key_pressed(KEY_LEFTCTRL, timestamp++);
    while (Test::cursor()->pos().x() > targetPos.x()) {
        Test::keyboard_key_pressed(KEY_LEFT, timestamp++);
        Test::keyboard_key_released(KEY_LEFT, timestamp++);
    }
    while (Test::cursor()->pos().x() < targetPos.x()) {
        Test::keyboard_key_pressed(KEY_RIGHT, timestamp++);
        Test::keyboard_key_released(KEY_RIGHT, timestamp++);
    }
    while (Test::cursor()->pos().y() < targetPos.y()) {
        Test::keyboard_key_pressed(KEY_DOWN, timestamp++);
        Test::keyboard_key_released(KEY_DOWN, timestamp++);
    }
    while (Test::cursor()->pos().y() > targetPos.y()) {
        Test::keyboard_key_pressed(KEY_UP, timestamp++);
        Test::keyboard_key_released(KEY_UP, timestamp++);
    }
    Test::keyboard_key_released(KEY_LEFTCTRL, timestamp++);
    Test::keyboard_key_pressed(KEY_ENTER, timestamp++);
    Test::keyboard_key_released(KEY_ENTER, timestamp++);
    QCOMPARE(Test::cursor()->pos(), targetPos);
    QVERIFY(!Test::app()->base.space->move_resize_window);

    QCOMPARE(quickTileChangedSpy.count(), 1);
    QTEST(c->control->quicktiling, "expectedMode");
}

void QuickTilingTest::testQuickTilingPointerMove_data()
{
    QTest::addColumn<QPoint>("targetPos");
    QTest::addColumn<win::quicktiles>("expectedMode");

    QTest::newRow("topRight") << QPoint(2559, 24)
                              << (win::quicktiles::top | win::quicktiles::right);
    QTest::newRow("right") << QPoint(2559, 512) << win::quicktiles::right;
    QTest::newRow("bottomRight") << QPoint(2559, 1023)
                                 << (win::quicktiles::bottom | win::quicktiles::right);
    QTest::newRow("bottomLeft") << QPoint(0, 1023)
                                << (win::quicktiles::bottom | win::quicktiles::left);
    QTest::newRow("Left") << QPoint(0, 512) << win::quicktiles::left;
    QTest::newRow("topLeft") << QPoint(0, 24) << (win::quicktiles::top | win::quicktiles::left);
}

void QuickTilingTest::testQuickTilingPointerMove()
{
    using namespace Wrapland::Client;

    auto surface = Test::create_surface();
    QVERIFY(surface);

    auto shellSurface = Test::create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    QVERIFY(shellSurface);

    // wait for the initial configure event
    QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    surface->commit(Surface::CommitFlag::None);
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    // let's render
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

    QVERIFY(c);
    QCOMPARE(Test::get_wayland_window(Test::app()->base.space->stacking.active), c);
    QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
    QCOMPARE(c->control->quicktiling, win::quicktiles::none);
    QCOMPARE(c->maximizeMode(), win::maximize_mode::restore);

    // we have to receive a configure event when the client becomes active
    QVERIFY(configureRequestedSpy.wait());
    QTRY_COMPARE(configureRequestedSpy.count(), 2);

    QSignalSpy quickTileChangedSpy(c->qobject.get(), &win::window_qobject::quicktiling_changed);
    QVERIFY(quickTileChangedSpy.isValid());

    win::perform_window_operation(c, base::options_qobject::UnrestrictedMoveOp);
    QCOMPARE(c, Test::get_wayland_window(Test::app()->base.space->move_resize_window));
    QCOMPARE(Test::cursor()->pos(), QPoint(49, 24));
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 3);

    QFETCH(QPoint, targetPos);
    quint32 timestamp = 1;
    Test::pointer_motion_absolute(targetPos, timestamp++);
    Test::pointer_button_pressed(BTN_LEFT, timestamp++);
    Test::pointer_button_released(BTN_LEFT, timestamp++);
    QCOMPARE(Test::cursor()->pos(), targetPos);
    QVERIFY(!Test::app()->base.space->move_resize_window);

    QCOMPARE(quickTileChangedSpy.count(), 1);
    QTEST(c->control->quicktiling, "expectedMode");
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 4);
    QCOMPARE(false, configureRequestedSpy.last().first().toSize().isEmpty());
}

void QuickTilingTest::testQuickTilingTouchMove_data()
{
    QTest::addColumn<QPoint>("targetPos");
    QTest::addColumn<win::quicktiles>("expectedMode");

    QTest::newRow("topRight") << QPoint(2559, 24)
                              << (win::quicktiles::top | win::quicktiles::right);
    QTest::newRow("right") << QPoint(2559, 512) << win::quicktiles::right;
    QTest::newRow("bottomRight") << QPoint(2559, 1023)
                                 << (win::quicktiles::bottom | win::quicktiles::right);
    QTest::newRow("bottomLeft") << QPoint(0, 1023)
                                << (win::quicktiles::bottom | win::quicktiles::left);
    QTest::newRow("Left") << QPoint(0, 512) << win::quicktiles::left;
    QTest::newRow("topLeft") << QPoint(0, 24) << (win::quicktiles::top | win::quicktiles::left);
}

void QuickTilingTest::testQuickTilingTouchMove()
{
    // test verifies that touch on decoration also allows quick tiling
    // see BUG: 390113
    using namespace Wrapland::Client;

    auto surface = Test::create_surface();
    QVERIFY(surface);

    auto shellSurface = Test::create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly);
    QVERIFY(shellSurface);

    auto deco = Test::get_client().interfaces.xdg_decoration->getToplevelDecoration(
        shellSurface.get(), shellSurface.get());
    QSignalSpy decoSpy(deco, &XdgDecoration::modeChanged);
    QVERIFY(decoSpy.isValid());

    deco->setMode(XdgDecoration::Mode::ServerSide);
    QCOMPARE(deco->mode(), XdgDecoration::Mode::ClientSide);

    QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());

    Test::init_xdg_shell_toplevel(surface, shellSurface);
    QCOMPARE(deco->mode(), XdgDecoration::Mode::ServerSide);
    QCOMPARE(configureRequestedSpy.count(), 1);
    QVERIFY(configureRequestedSpy.last().first().toSize().isEmpty());

    // let's render
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    auto c = Test::render_and_wait_for_shown(surface, QSize(1000, 50), Qt::blue);

    QVERIFY(c);
    QVERIFY(win::decoration(c));
    auto const decoration = win::decoration(c);
    QCOMPARE(Test::get_wayland_window(Test::app()->base.space->stacking.active), c);
    QCOMPARE(c->geo.frame,
             QRect(-decoration->borderLeft(),
                   0,
                   1000 + decoration->borderLeft() + decoration->borderRight(),
                   50 + decoration->borderTop() + decoration->borderBottom()));
    QCOMPARE(c->control->quicktiling, win::quicktiles::none);
    QCOMPARE(c->maximizeMode(), win::maximize_mode::restore);

    // we have to receive a configure event when the client becomes active
    QVERIFY(configureRequestedSpy.wait());
    QTRY_COMPARE(configureRequestedSpy.count(), 2);

    QSignalSpy quickTileChangedSpy(c->qobject.get(), &win::window_qobject::quicktiling_changed);
    QVERIFY(quickTileChangedSpy.isValid());

    quint32 timestamp = 1;
    Test::touch_down(
        0,
        QPointF(c->geo.frame.center().x(), c->geo.frame.y() + decoration->borderTop() / 2),
        timestamp++);
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(c, Test::get_wayland_window(Test::app()->base.space->move_resize_window));
    QCOMPARE(configureRequestedSpy.count(), 3);

    QFETCH(QPoint, targetPos);
    Test::touch_motion(0, targetPos, timestamp++);
    Test::touch_up(0, timestamp++);
    QVERIFY(!Test::app()->base.space->move_resize_window);

    // When there are no borders, there is no change to them when quick-tiling.
    // TODO: we should test both cases with fixed fake decoration for autotests.
    auto const hasBorders
        = Test::app()->base.space->deco->settings()->borderSize() != KDecoration2::BorderSize::None;

    QCOMPARE(quickTileChangedSpy.count(), 1);
    QTEST(c->control->quicktiling, "expectedMode");
    QVERIFY(configureRequestedSpy.wait());
    QTRY_COMPARE(configureRequestedSpy.count(), hasBorders ? 5 : 4);
    QCOMPARE(false, configureRequestedSpy.last().first().toSize().isEmpty());
}

void QuickTilingTest::testX11QuickTiling_data()
{
    QTest::addColumn<win::quicktiles>("mode");
    QTest::addColumn<QRect>("expectedGeometry");
    QTest::addColumn<int>("screen");
    QTest::addColumn<win::quicktiles>("modeAfterToggle");

#define FLAG(name) QuickTileMode(QuickTileFlag::name)

    QTest::newRow("left") << win::quicktiles::left << QRect(0, 0, 640, 1024) << 0
                          << win::quicktiles::none;
    QTest::newRow("top") << win::quicktiles::top << QRect(0, 0, 1280, 512) << 1
                         << win::quicktiles::top;
    QTest::newRow("right") << win::quicktiles::right << QRect(640, 0, 640, 1024) << 1
                           << win::quicktiles::left;
    QTest::newRow("bottom") << win::quicktiles::bottom << QRect(0, 512, 1280, 512) << 1
                            << win::quicktiles::bottom;

    QTest::newRow("top left") << (win::quicktiles::left | win::quicktiles::top)
                              << QRect(0, 0, 640, 512) << 0 << win::quicktiles::none;
    QTest::newRow("top right") << (win::quicktiles::right | win::quicktiles::top)
                               << QRect(640, 0, 640, 512) << 1
                               << (win::quicktiles::left | win::quicktiles::top);
    QTest::newRow("bottom left") << (win::quicktiles::left | win::quicktiles::bottom)
                                 << QRect(0, 512, 640, 512) << 0 << win::quicktiles::none;
    QTest::newRow("bottom right") << (win::quicktiles::right | win::quicktiles::bottom)
                                  << QRect(640, 512, 640, 512) << 1
                                  << (win::quicktiles::left | win::quicktiles::bottom);

    QTest::newRow("maximize") << win::quicktiles::maximize << QRect(0, 0, 1280, 1024) << 0
                              << win::quicktiles::none;

#undef FLAG
}
void QuickTilingTest::testX11QuickTiling()
{
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
    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());

    auto client = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(client);
    QCOMPARE(client->xcb_windows.client, w);

    auto const origGeo = client->geo.frame;

    // now quick tile
    QSignalSpy quickTileChangedSpy(client->qobject.get(),
                                   &win::window_qobject::quicktiling_changed);
    QVERIFY(quickTileChangedSpy.isValid());

    QFETCH(win::quicktiles, mode);
    win::set_quicktile_mode(client, mode, true);

    QCOMPARE(client->control->quicktiling, mode);
    QTEST(client->geo.frame, "expectedGeometry");
    QCOMPARE(client->geo.restore.max, origGeo);
    QCOMPARE(quickTileChangedSpy.count(), 1);

    QCOMPARE(client->topo.central_output, Test::app()->base.outputs.at(0));
    QFETCH(win::quicktiles, modeAfterToggle);

    // quick tile to same edge again should also act like send to screen
    win::set_quicktile_mode(client, mode, true);
    QTEST(static_cast<int>(
              base::get_output_index(Test::app()->base.outputs, *client->topo.central_output)),
          "screen");
    QCOMPARE(client->control->quicktiling, modeAfterToggle);
    QCOMPARE(client->geo.restore.max.isValid(), modeAfterToggle != win::quicktiles::none);
    QCOMPARE(client->geo.restore.max, modeAfterToggle != win::quicktiles::none ? origGeo : QRect());

    // and destroy the window again
    xcb_unmap_window(c.get(), w);
    xcb_destroy_window(c.get(), w);
    xcb_flush(c.get());
    c.reset();

    QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
    QVERIFY(windowClosedSpy.isValid());
    QVERIFY(windowClosedSpy.wait());
}

void QuickTilingTest::testX11QuickTilingAfterVertMaximize_data()
{
    QTest::addColumn<win::quicktiles>("mode");
    QTest::addColumn<QRect>("expectedGeometry");

    QTest::newRow("left") << win::quicktiles::left << QRect(0, 0, 640, 1024);
    QTest::newRow("top") << win::quicktiles::top << QRect(0, 0, 1280, 512);
    QTest::newRow("right") << win::quicktiles::right << QRect(640, 0, 640, 1024);
    QTest::newRow("bottom") << win::quicktiles::bottom << QRect(0, 512, 1280, 512);

    QTest::newRow("top left") << (win::quicktiles::left | win::quicktiles::top)
                              << QRect(0, 0, 640, 512);
    QTest::newRow("top right") << (win::quicktiles::right | win::quicktiles::top)
                               << QRect(640, 0, 640, 512);
    QTest::newRow("bottom left") << (win::quicktiles::left | win::quicktiles::bottom)
                                 << QRect(0, 512, 640, 512);
    QTest::newRow("bottom right") << (win::quicktiles::right | win::quicktiles::bottom)
                                  << QRect(640, 512, 640, 512);

    QTest::newRow("maximize") << win::quicktiles::maximize << QRect(0, 0, 1280, 1024);
}

void QuickTilingTest::testX11QuickTilingAfterVertMaximize()
{
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
    QSignalSpy windowCreatedSpy(Test::app()->base.space->qobject.get(),
                                &win::space::qobject_t::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());

    auto client = get_x11_window_from_id(windowCreatedSpy.first().first().value<quint32>());
    QVERIFY(client);
    QCOMPARE(client->xcb_windows.client, w);

    const QRect origGeo = client->geo.frame;
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
    // vertically maximize the window
    win::maximize(client, flags(client->maximizeMode() ^ win::maximize_mode::vertical));
    QCOMPARE(client->geo.frame.width(), origGeo.width());
    QCOMPARE(client->geo.size().height(), client->topo.central_output->geometry().height());
    QCOMPARE(client->geo.restore.max, origGeo);

    // now quick tile
    QSignalSpy quickTileChangedSpy(client->qobject.get(),
                                   &win::window_qobject::quicktiling_changed);
    QVERIFY(quickTileChangedSpy.isValid());
    QFETCH(win::quicktiles, mode);
    win::set_quicktile_mode(client, mode, true);
    QCOMPARE(client->control->quicktiling, mode);
    QTEST(client->geo.frame, "expectedGeometry");
    QCOMPARE(quickTileChangedSpy.count(), 1);

    // and destroy the window again
    xcb_unmap_window(c.get(), w);
    xcb_destroy_window(c.get(), w);
    xcb_flush(c.get());
    c.reset();

    QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
    QVERIFY(windowClosedSpy.isValid());
    QVERIFY(windowClosedSpy.wait());
}

void QuickTilingTest::testShortcut_data()
{
    QTest::addColumn<QStringList>("shortcutList");
    QTest::addColumn<win::quicktiles>("expectedMode");
    QTest::addColumn<QRect>("expectedGeometry");

#define FLAG(name) QuickTileMode(QuickTileFlag::name)
    QTest::newRow("top") << QStringList{QStringLiteral("Window Quick Tile Top")}
                         << win::quicktiles::top << QRect(0, 0, 1280, 512);
    QTest::newRow("bottom") << QStringList{QStringLiteral("Window Quick Tile Bottom")}
                            << win::quicktiles::bottom << QRect(0, 512, 1280, 512);
    QTest::newRow("top right") << QStringList{QStringLiteral("Window Quick Tile Top Right")}
                               << (win::quicktiles::top | win::quicktiles::right)
                               << QRect(640, 0, 640, 512);
    QTest::newRow("top left") << QStringList{QStringLiteral("Window Quick Tile Top Left")}
                              << (win::quicktiles::top | win::quicktiles::left)
                              << QRect(0, 0, 640, 512);
    QTest::newRow("bottom right") << QStringList{QStringLiteral("Window Quick Tile Bottom Right")}
                                  << (win::quicktiles::bottom | win::quicktiles::right)
                                  << QRect(640, 512, 640, 512);
    QTest::newRow("bottom left") << QStringList{QStringLiteral("Window Quick Tile Bottom Left")}
                                 << (win::quicktiles::bottom | win::quicktiles::left)
                                 << QRect(0, 512, 640, 512);
    QTest::newRow("left") << QStringList{QStringLiteral("Window Quick Tile Left")}
                          << win::quicktiles::left << QRect(0, 0, 640, 1024);
    QTest::newRow("right") << QStringList{QStringLiteral("Window Quick Tile Right")}
                           << win::quicktiles::right << QRect(640, 0, 640, 1024);

    // Test combined actions for corner tiling
    QTest::newRow("top left combined")
        << QStringList{QStringLiteral("Window Quick Tile Left"),
                       QStringLiteral("Window Quick Tile Top")}
        << (win::quicktiles::top | win::quicktiles::left) << QRect(0, 0, 640, 512);
    QTest::newRow("top right combined")
        << QStringList{QStringLiteral("Window Quick Tile Right"),
                       QStringLiteral("Window Quick Tile Top")}
        << (win::quicktiles::top | win::quicktiles::right) << QRect(640, 0, 640, 512);
    QTest::newRow("bottom left combined")
        << QStringList{QStringLiteral("Window Quick Tile Left"),
                       QStringLiteral("Window Quick Tile Bottom")}
        << (win::quicktiles::bottom | win::quicktiles::left) << QRect(0, 512, 640, 512);
    QTest::newRow("bottom right combined")
        << QStringList{QStringLiteral("Window Quick Tile Right"),
                       QStringLiteral("Window Quick Tile Bottom")}
        << (win::quicktiles::bottom | win::quicktiles::right) << QRect(640, 512, 640, 512);
#undef FLAG
}

void QuickTilingTest::testShortcut()
{
    using namespace Wrapland::Client;

    auto surface = Test::create_surface();
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);

    // Map the client.
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(Test::get_wayland_window(Test::app()->base.space->stacking.active), c);
    QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
    QCOMPARE(c->control->quicktiling, win::quicktiles::none);

    // We have to receive a configure event when the client becomes active.
    QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    QFETCH(QStringList, shortcutList);
    QFETCH(QRect, expectedGeometry);

    const int numberOfQuickTileActions = shortcutList.count();

    if (numberOfQuickTileActions > 1) {
        QTest::qWait(1001);
    }

    for (const QString& shortcut : shortcutList) {
        // invoke global shortcut through dbus
        auto msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kglobalaccel"),
                                                  QStringLiteral("/component/kwin"),
                                                  QStringLiteral("org.kde.kglobalaccel.Component"),
                                                  QStringLiteral("invokeShortcut"));
        msg.setArguments(QList<QVariant>{shortcut});
        QDBusConnection::sessionBus().asyncCall(msg);
    }

    QSignalSpy quickTileChangedSpy(c->qobject.get(), &win::window_qobject::quicktiling_changed);
    QVERIFY(quickTileChangedSpy.isValid());
    QTRY_COMPARE(quickTileChangedSpy.count(), numberOfQuickTileActions);
    // at this point the geometry did not yet change
    QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
    // but quick tile mode already changed
    QTEST(c->control->quicktiling, "expectedMode");

    // but we got requested a new geometry
    QTRY_COMPARE(configureRequestedSpy.count(), numberOfQuickTileActions + 1);
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), expectedGeometry.size());

    // attach a new image
    QSignalSpy geometryChangedSpy(c->qobject.get(), &win::window_qobject::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface, expectedGeometry.size(), Qt::red);

    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(geometryChangedSpy.count(), 1);
    QCOMPARE(c->geo.frame, expectedGeometry);
}

void QuickTilingTest::testScript_data()
{
    QTest::addColumn<QString>("action");
    QTest::addColumn<win::quicktiles>("expectedMode");
    QTest::addColumn<QRect>("expectedGeometry");

#define FLAG(name) QuickTileMode(QuickTileFlag::name)
    QTest::newRow("top") << QStringLiteral("Top") << win::quicktiles::top << QRect(0, 0, 1280, 512);
    QTest::newRow("bottom") << QStringLiteral("Bottom") << win::quicktiles::bottom
                            << QRect(0, 512, 1280, 512);
    QTest::newRow("top right") << QStringLiteral("TopRight")
                               << (win::quicktiles::top | win::quicktiles::right)
                               << QRect(640, 0, 640, 512);
    QTest::newRow("top left") << QStringLiteral("TopLeft")
                              << (win::quicktiles::top | win::quicktiles::left)
                              << QRect(0, 0, 640, 512);
    QTest::newRow("bottom right") << QStringLiteral("BottomRight")
                                  << (win::quicktiles::bottom | win::quicktiles::right)
                                  << QRect(640, 512, 640, 512);
    QTest::newRow("bottom left") << QStringLiteral("BottomLeft")
                                 << (win::quicktiles::bottom | win::quicktiles::left)
                                 << QRect(0, 512, 640, 512);
    QTest::newRow("left") << QStringLiteral("Left") << win::quicktiles::left
                          << QRect(0, 0, 640, 1024);
    QTest::newRow("right") << QStringLiteral("Right") << win::quicktiles::right
                           << QRect(640, 0, 640, 1024);
#undef FLAG
}

void QuickTilingTest::testScript()
{
    using namespace Wrapland::Client;

    auto surface = Test::create_surface();
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);

    // Map the client.
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(Test::get_wayland_window(Test::app()->base.space->stacking.active), c);
    QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
    QCOMPARE(c->control->quicktiling, win::quicktiles::none);

    // We have to receive a configure event upon the client becoming active.
    QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);

    QSignalSpy quickTileChangedSpy(c->qobject.get(), &win::window_qobject::quicktiling_changed);
    QVERIFY(quickTileChangedSpy.isValid());
    QSignalSpy geometryChangedSpy(c->qobject.get(), &win::window_qobject::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());

    QVERIFY(Test::app()->base.space->scripting);
    QTemporaryFile tmpFile;
    QVERIFY(tmpFile.open());
    QTextStream out(&tmpFile);

    QFETCH(QString, action);
    out << "workspace.slotWindowQuickTile" << action << "()";
    out.flush();

    QFETCH(win::quicktiles, expectedMode);
    QFETCH(QRect, expectedGeometry);

    auto const id = Test::app()->base.space->scripting->loadScript(tmpFile.fileName());
    QVERIFY(id != -1);
    QVERIFY(Test::app()->base.space->scripting->isScriptLoaded(tmpFile.fileName()));
    auto s = Test::app()->base.space->scripting->findScript(tmpFile.fileName());
    QVERIFY(s);
    QSignalSpy runningChangedSpy(s, &scripting::abstract_script::runningChanged);
    QVERIFY(runningChangedSpy.isValid());
    s->run();

    QVERIFY(quickTileChangedSpy.wait());
    QCOMPARE(quickTileChangedSpy.count(), 1);

    QCOMPARE(runningChangedSpy.count(), 1);
    QCOMPARE(runningChangedSpy.first().first().toBool(), true);

    // at this point the geometry did not yet change
    QCOMPARE(c->geo.frame, QRect(0, 0, 100, 50));
    // but quick tile mode already changed
    QCOMPARE(c->control->quicktiling, expectedMode);

    // but we got requested a new geometry
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 2);
    QCOMPARE(configureRequestedSpy.last().at(0).toSize(), expectedGeometry.size());

    // attach a new image
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface, expectedGeometry.size(), Qt::red);

    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(geometryChangedSpy.count(), 1);
    QCOMPARE(c->geo.frame, expectedGeometry);
}

}

WAYLANDTEST_MAIN(KWin::QuickTilingTest)
#include "quick_tiling_test.moc"
