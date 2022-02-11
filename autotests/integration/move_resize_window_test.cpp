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
#include "base/x11/atoms.h"
#include "input/cursor.h"
#include "render/effects.h"
#include "screens.h"
#include "toplevel.h"
#include "win/input.h"
#include "win/move.h"
#include "win/placement.h"
#include "win/wayland/window.h"
#include "win/x11/window.h"
#include "workspace.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/plasmashell.h>
#include <Wrapland/Client/pointer.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>

#include <linux/input.h>
#include <xcb/xcb_icccm.h>

Q_DECLARE_METATYPE(KWin::win::quicktiles)

namespace KWin
{

class MoveResizeWindowTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testMove();
    void testResize();
    void testPackTo_data();
    void testPackTo();
    void testPackAgainstClient_data();
    void testPackAgainstClient();
    void testGrowShrink_data();
    void testGrowShrink();
    void testPointerMoveEnd_data();
    void testPointerMoveEnd();
    void testClientSideMove();
    void testPlasmaShellSurfaceMovable_data();
    void testPlasmaShellSurfaceMovable();
    void testNetMove();
    void testAdjustClientGeometryOfAutohidingX11Panel_data();
    void testAdjustClientGeometryOfAutohidingX11Panel();
    void testAdjustClientGeometryOfAutohidingWaylandPanel_data();
    void testAdjustClientGeometryOfAutohidingWaylandPanel();
    void testDestroyMoveClient();
    void testDestroyResizeClient();
    void testUnmapMoveClient();
    void testUnmapResizeClient();
    void testSetFullScreenWhenMoving();
    void testSetMaximizeWhenMoving();

private:
    Wrapland::Client::ConnectionThread* m_connection = nullptr;
    Wrapland::Client::Compositor* m_compositor = nullptr;
};

void MoveResizeWindowTest::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();
    qRegisterMetaType<KWin::win::x11::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    QVERIFY(startup_spy.wait());
    QCOMPARE(Test::app()->base.screens.count(), 1);
    QCOMPARE(Test::app()->base.screens.geometry(0), QRect(0, 0, 1280, 1024));
}

void MoveResizeWindowTest::init()
{
    Test::setup_wayland_connection(Test::global_selection::plasma_shell
                                   | Test::global_selection::seat);
    QVERIFY(Test::wait_for_wayland_pointer());
    m_connection = Test::get_client().connection;
    m_compositor = Test::get_client().interfaces.compositor.get();

    Test::app()->base.screens.setCurrent(0);
}

void MoveResizeWindowTest::cleanup()
{
    Test::destroy_wayland_connection();
}

void MoveResizeWindowTest::testMove()
{
    using namespace Wrapland::Client;

    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);

    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    QSignalSpy sizeChangeSpy(shellSurface.get(), &XdgShellToplevel::sizeChanged);
    QVERIFY(sizeChangeSpy.isValid());

    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(workspace()->activeClient(), c);
    QCOMPARE(c->frameGeometry(), QRect(0, 0, 100, 50));

    QSignalSpy geometryChangedSpy(c, &Toplevel::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());
    QSignalSpy startMoveResizedSpy(c, &Toplevel::clientStartUserMovedResized);
    QVERIFY(startMoveResizedSpy.isValid());
    QSignalSpy moveResizedChangedSpy(c, &Toplevel::moveResizedChanged);
    QVERIFY(moveResizedChangedSpy.isValid());
    QSignalSpy clientStepUserMovedResizedSpy(c, &Toplevel::clientStepUserMovedResized);
    QVERIFY(clientStepUserMovedResizedSpy.isValid());
    QSignalSpy clientFinishUserMovedResizedSpy(c, &Toplevel::clientFinishUserMovedResized);
    QVERIFY(clientFinishUserMovedResizedSpy.isValid());

    // effects signal handlers
    QSignalSpy windowStartUserMovedResizedSpy(effects,
                                              &EffectsHandler::windowStartUserMovedResized);
    QVERIFY(windowStartUserMovedResizedSpy.isValid());
    QSignalSpy windowStepUserMovedResizedSpy(effects, &EffectsHandler::windowStepUserMovedResized);
    QVERIFY(windowStepUserMovedResizedSpy.isValid());
    QSignalSpy windowFinishUserMovedResizedSpy(effects,
                                               &EffectsHandler::windowFinishUserMovedResized);
    QVERIFY(windowFinishUserMovedResizedSpy.isValid());

    QVERIFY(workspace()->moveResizeClient() == nullptr);
    QCOMPARE(win::is_move(c), false);

    // begin move
    workspace()->slotWindowMove();
    QCOMPARE(workspace()->moveResizeClient(), c);
    QCOMPARE(startMoveResizedSpy.count(), 1);
    QCOMPARE(moveResizedChangedSpy.count(), 1);
    QCOMPARE(windowStartUserMovedResizedSpy.count(), 1);
    QCOMPARE(win::is_move(c), true);
    QCOMPARE(c->restore_geometries.maximize, QRect(0, 0, 100, 50));

    // send some key events, not going through input redirection
    auto const cursorPos = input::get_cursor()->pos();
    win::key_press_event(c, Qt::Key_Right);
    win::update_move_resize(c, input::get_cursor()->pos());
    QCOMPARE(input::get_cursor()->pos(), cursorPos + QPoint(8, 0));
    QEXPECT_FAIL("", "First event is ignored", Continue);
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);
    clientStepUserMovedResizedSpy.clear();
    windowStepUserMovedResizedSpy.clear();

    win::key_press_event(c, Qt::Key_Right);
    win::update_move_resize(c, input::get_cursor()->pos());
    QCOMPARE(input::get_cursor()->pos(), cursorPos + QPoint(16, 0));
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);
    QCOMPARE(windowStepUserMovedResizedSpy.count(), 1);

    win::key_press_event(c, Qt::Key_Down | Qt::ALT);
    win::update_move_resize(c, input::get_cursor()->pos());
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 2);
    QCOMPARE(windowStepUserMovedResizedSpy.count(), 2);
    QCOMPARE(c->frameGeometry(), QRect(16, 32, 100, 50));
    QCOMPARE(input::get_cursor()->pos(), cursorPos + QPoint(16, 32));

    // let's end
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 0);
    win::key_press_event(c, Qt::Key_Enter);
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);
    QCOMPARE(moveResizedChangedSpy.count(), 2);
    QCOMPARE(windowFinishUserMovedResizedSpy.count(), 1);
    QCOMPARE(c->frameGeometry(), QRect(16, 32, 100, 50));
    QCOMPARE(win::is_move(c), false);
    QVERIFY(workspace()->moveResizeClient() == nullptr);
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(c));
}

void MoveResizeWindowTest::testResize()
{
    // a test case which manually resizes a window
    using namespace Wrapland::Client;

    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);

    std::unique_ptr<XdgShellToplevel> shellSurface(
        Test::create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly));
    QVERIFY(shellSurface);

    // Wait for the initial configure event.
    XdgShellToplevel::States states;
    QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    surface->commit(Surface::CommitFlag::None);
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);
    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Resizing));

    // Let's render.
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QSignalSpy surfaceSizeChangedSpy(shellSurface.get(), &XdgShellToplevel::sizeChanged);
    QVERIFY(surfaceSizeChangedSpy.isValid());

    // We have to receive a configure event when the client becomes active.
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 2);
    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Resizing));
    QCOMPARE(surfaceSizeChangedSpy.count(), 1);

    QVERIFY(c);
    QCOMPARE(workspace()->activeClient(), c);
    QCOMPARE(c->frameGeometry(), QRect(0, 0, 100, 50));
    QSignalSpy geometryChangedSpy(c, &Toplevel::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());
    QSignalSpy startMoveResizedSpy(c, &Toplevel::clientStartUserMovedResized);
    QVERIFY(startMoveResizedSpy.isValid());
    QSignalSpy moveResizedChangedSpy(c, &Toplevel::moveResizedChanged);
    QVERIFY(moveResizedChangedSpy.isValid());
    QSignalSpy clientStepUserMovedResizedSpy(c, &Toplevel::clientStepUserMovedResized);
    QVERIFY(clientStepUserMovedResizedSpy.isValid());
    QSignalSpy clientFinishUserMovedResizedSpy(c, &Toplevel::clientFinishUserMovedResized);
    QVERIFY(clientFinishUserMovedResizedSpy.isValid());

    // begin resize
    QCOMPARE(workspace()->moveResizeClient(), nullptr);
    QCOMPARE(win::is_move(c), false);
    QCOMPARE(win::is_resize(c), false);
    workspace()->slotWindowResize();
    QCOMPARE(workspace()->moveResizeClient(), c);
    QCOMPARE(startMoveResizedSpy.count(), 1);
    QCOMPARE(moveResizedChangedSpy.count(), 1);
    QCOMPARE(win::is_resize(c), true);
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 3);
    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Resizing));

    // Trigger a change.
    auto const cursorPos = input::get_cursor()->pos();
    win::key_press_event(c, Qt::Key_Right);
    win::update_move_resize(c, input::get_cursor()->pos());
    QCOMPARE(input::get_cursor()->pos(), cursorPos + QPoint(8, 0));

    // The client should receive a configure event with the new size.
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 4);
    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Resizing));
    QCOMPARE(surfaceSizeChangedSpy.count(), 2);
    QCOMPARE(surfaceSizeChangedSpy.last().first().toSize(), QSize(108, 50));
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 0);

    // Now render new size.
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface, QSize(108, 50), Qt::blue);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(c->frameGeometry(), QRect(0, 0, 108, 50));
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);

    // Go down.
    win::key_press_event(c, Qt::Key_Down);
    win::update_move_resize(c, input::get_cursor()->pos());
    QCOMPARE(input::get_cursor()->pos(), cursorPos + QPoint(8, 8));

    // The client should receive another configure event.
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 5);
    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Resizing));
    QCOMPARE(surfaceSizeChangedSpy.count(), 3);
    QCOMPARE(surfaceSizeChangedSpy.last().first().toSize(), QSize(108, 58));

    // Now render new size.
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface, QSize(108, 58), Qt::blue);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(c->frameGeometry(), QRect(0, 0, 108, 58));
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 2);

    // Let's finalize the resize operation.
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 0);
    win::key_press_event(c, Qt::Key_Enter);
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 1);
    QCOMPARE(moveResizedChangedSpy.count(), 2);
    QCOMPARE(win::is_resize(c), false);
    QCOMPARE(workspace()->moveResizeClient(), nullptr);
    QEXPECT_FAIL("", "XdgShellClient currently doesn't send final configure event", Abort);
    QVERIFY(configureRequestedSpy.wait(500));
    QCOMPARE(configureRequestedSpy.count(), 6);
    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Resizing));

    // Destroy the client.
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(c));
}

void MoveResizeWindowTest::testPackTo_data()
{
    QTest::addColumn<QString>("methodCall");
    QTest::addColumn<QRect>("expectedGeometry");

    QTest::newRow("left") << QStringLiteral("slotWindowPackLeft") << QRect(0, 487, 100, 50);
    QTest::newRow("up") << QStringLiteral("slotWindowPackUp") << QRect(590, 0, 100, 50);
    QTest::newRow("right") << QStringLiteral("slotWindowPackRight") << QRect(1180, 487, 100, 50);
    QTest::newRow("down") << QStringLiteral("slotWindowPackDown") << QRect(590, 974, 100, 50);
}

void MoveResizeWindowTest::testPackTo()
{
    using namespace Wrapland::Client;

    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);

    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    QSignalSpy sizeChangeSpy(shellSurface.get(), &XdgShellToplevel::sizeChanged);
    QVERIFY(sizeChangeSpy.isValid());
    // let's render
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

    QVERIFY(c);
    QCOMPARE(workspace()->activeClient(), c);
    QCOMPARE(c->frameGeometry(), QRect(0, 0, 100, 50));

    // let's place it centered
    win::place_centered(c, QRect(0, 0, 1280, 1024));
    QCOMPARE(c->frameGeometry(), QRect(590, 487, 100, 50));

    QFETCH(QString, methodCall);
    QMetaObject::invokeMethod(workspace(), methodCall.toLocal8Bit().constData());
    QTEST(c->frameGeometry(), "expectedGeometry");
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(c));
}

void MoveResizeWindowTest::testPackAgainstClient_data()
{
    QTest::addColumn<QString>("methodCall");
    QTest::addColumn<QRect>("expectedGeometry");

    QTest::newRow("left") << QStringLiteral("slotWindowPackLeft") << QRect(10, 487, 100, 50);
    QTest::newRow("up") << QStringLiteral("slotWindowPackUp") << QRect(590, 10, 100, 50);
    QTest::newRow("right") << QStringLiteral("slotWindowPackRight") << QRect(1170, 487, 100, 50);
    QTest::newRow("down") << QStringLiteral("slotWindowPackDown") << QRect(590, 964, 100, 50);
}

void MoveResizeWindowTest::testPackAgainstClient()
{
    using namespace Wrapland::Client;

    std::unique_ptr<Surface> surface1(Test::create_surface());
    QVERIFY(surface1);
    std::unique_ptr<Surface> surface2(Test::create_surface());
    QVERIFY(surface2);
    std::unique_ptr<Surface> surface3(Test::create_surface());
    QVERIFY(surface3);
    std::unique_ptr<Surface> surface4(Test::create_surface());
    QVERIFY(surface4);

    std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
    QVERIFY(shellSurface1);
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    QVERIFY(shellSurface2);
    std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
    QVERIFY(shellSurface3);
    std::unique_ptr<XdgShellToplevel> shellSurface4(Test::create_xdg_shell_toplevel(surface4));
    QVERIFY(shellSurface4);
    auto renderWindow = [this](std::unique_ptr<Surface> const& surface,
                               const QString& methodCall,
                               const QRect& expectedGeometry) {
        // let's render
        auto c = Test::render_and_wait_for_shown(surface, QSize(10, 10), Qt::blue);

        QVERIFY(c);
        QCOMPARE(workspace()->activeClient(), c);
        QCOMPARE(c->frameGeometry().size(), QSize(10, 10));
        // let's place it centered
        win::place_centered(c, QRect(0, 0, 1280, 1024));
        QCOMPARE(c->frameGeometry(), QRect(635, 507, 10, 10));
        QMetaObject::invokeMethod(workspace(), methodCall.toLocal8Bit().constData());
        QCOMPARE(c->frameGeometry(), expectedGeometry);
    };
    renderWindow(surface1, QStringLiteral("slotWindowPackLeft"), QRect(0, 507, 10, 10));
    renderWindow(surface2, QStringLiteral("slotWindowPackUp"), QRect(635, 0, 10, 10));
    renderWindow(surface3, QStringLiteral("slotWindowPackRight"), QRect(1270, 507, 10, 10));
    renderWindow(surface4, QStringLiteral("slotWindowPackDown"), QRect(635, 1014, 10, 10));

    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

    QVERIFY(c);
    QCOMPARE(workspace()->activeClient(), c);
    // let's place it centered
    win::place_centered(c, QRect(0, 0, 1280, 1024));
    QCOMPARE(c->frameGeometry(), QRect(590, 487, 100, 50));

    QFETCH(QString, methodCall);
    QMetaObject::invokeMethod(workspace(), methodCall.toLocal8Bit().constData());
    QTEST(c->frameGeometry(), "expectedGeometry");
}

void MoveResizeWindowTest::testGrowShrink_data()
{
    QTest::addColumn<QString>("methodCall");
    QTest::addColumn<QRect>("expectedGeometry");

    QTest::newRow("grow vertical")
        << QStringLiteral("slotWindowGrowVertical") << QRect(590, 487, 100, 537);
    QTest::newRow("grow horizontal")
        << QStringLiteral("slotWindowGrowHorizontal") << QRect(590, 487, 690, 50);
    QTest::newRow("shrink vertical")
        << QStringLiteral("slotWindowShrinkVertical") << QRect(590, 487, 100, 23);
    QTest::newRow("shrink horizontal")
        << QStringLiteral("slotWindowShrinkHorizontal") << QRect(590, 487, 40, 50);
}

void MoveResizeWindowTest::testGrowShrink()
{
    using namespace Wrapland::Client;

    // This helper surface ensures the test surface will shrink when calling the respective methods.
    std::unique_ptr<Surface> surface1(Test::create_surface());
    QVERIFY(surface1);
    std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
    QVERIFY(shellSurface1);
    auto window = Test::render_and_wait_for_shown(surface1, QSize(650, 514), Qt::blue);
    QVERIFY(window);
    workspace()->slotWindowPackRight();
    workspace()->slotWindowPackDown();

    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);

    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    QSignalSpy configure_spy(shellSurface.get(), &XdgShellToplevel::configureRequested);
    QVERIFY(configure_spy.isValid());
    QSignalSpy sizeChangeSpy(shellSurface.get(), &XdgShellToplevel::sizeChanged);
    QVERIFY(sizeChangeSpy.isValid());

    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(workspace()->activeClient(), c);

    // Configure event due to activation.
    QVERIFY(configure_spy.wait());
    QCOMPARE(configure_spy.count(), 1);

    QSignalSpy geometryChangedSpy(c, &Toplevel::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());

    win::place_centered(c, QRect(0, 0, 1280, 1024));
    QCOMPARE(c->frameGeometry(), QRect(590, 487, 100, 50));

    // Now according to test data grow/shrink vertically/horizontally.
    QFETCH(QString, methodCall);
    QMetaObject::invokeMethod(workspace(), methodCall.toLocal8Bit().constData());

    QVERIFY(sizeChangeSpy.wait());
    QCOMPARE(configure_spy.count(), 2);

    shellSurface->ackConfigure(configure_spy.last().at(2).value<quint32>());
    QCOMPARE(shellSurface->size(), configure_spy.last().first().toSize());
    Test::render(surface, shellSurface->size(), Qt::red);

    QVERIFY(geometryChangedSpy.wait());
    QTEST(c->frameGeometry(), "expectedGeometry");
}

void MoveResizeWindowTest::testPointerMoveEnd_data()
{
    QTest::addColumn<int>("additionalButton");

    QTest::newRow("BTN_RIGHT") << BTN_RIGHT;
    QTest::newRow("BTN_MIDDLE") << BTN_MIDDLE;
    QTest::newRow("BTN_SIDE") << BTN_SIDE;
    QTest::newRow("BTN_EXTRA") << BTN_EXTRA;
    QTest::newRow("BTN_FORWARD") << BTN_FORWARD;
    QTest::newRow("BTN_BACK") << BTN_BACK;
    QTest::newRow("BTN_TASK") << BTN_TASK;
    for (int i = BTN_TASK + 1; i < BTN_JOYSTICK; i++) {
        QTest::newRow(QByteArray::number(i, 16).constData()) << i;
    }
}

void MoveResizeWindowTest::testPointerMoveEnd()
{
    // this test verifies that moving a window through pointer only ends if all buttons are released
    using namespace Wrapland::Client;

    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);

    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    QSignalSpy sizeChangeSpy(shellSurface.get(), &XdgShellToplevel::sizeChanged);
    QVERIFY(sizeChangeSpy.isValid());
    // let's render
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

    QVERIFY(c);
    QCOMPARE(c, workspace()->activeClient());
    QVERIFY(!win::is_move(c));

    // let's trigger the left button
    quint32 timestamp = 1;
    Test::pointer_button_pressed(BTN_LEFT, timestamp++);
    QVERIFY(!win::is_move(c));
    workspace()->slotWindowMove();
    QVERIFY(win::is_move(c));

    // let's press another button
    QFETCH(int, additionalButton);
    Test::pointer_button_pressed(additionalButton, timestamp++);
    QVERIFY(win::is_move(c));

    // release the left button, should still have the window moving
    Test::pointer_button_released(BTN_LEFT, timestamp++);
    QVERIFY(win::is_move(c));

    // but releasing the other button should now end moving
    Test::pointer_button_released(additionalButton, timestamp++);
    QVERIFY(!win::is_move(c));
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(c));
}

void MoveResizeWindowTest::testClientSideMove()
{
    using namespace Wrapland::Client;
    input::get_cursor()->set_pos(640, 512);
    std::unique_ptr<Pointer> pointer(Test::get_client().interfaces.seat->createPointer());
    QSignalSpy pointerEnteredSpy(pointer.get(), &Pointer::entered);
    QVERIFY(pointerEnteredSpy.isValid());
    QSignalSpy pointerLeftSpy(pointer.get(), &Pointer::left);
    QVERIFY(pointerLeftSpy.isValid());
    QSignalSpy buttonSpy(pointer.get(), &Pointer::buttonStateChanged);
    QVERIFY(buttonSpy.isValid());

    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);

    // move pointer into center of geometry
    const QRect startGeometry = c->frameGeometry();
    input::get_cursor()->set_pos(startGeometry.center());
    QVERIFY(pointerEnteredSpy.wait());
    QCOMPARE(pointerEnteredSpy.first().last().toPoint(), QPoint(49, 24));
    // simulate press
    quint32 timestamp = 1;
    Test::pointer_button_pressed(BTN_LEFT, timestamp++);
    QVERIFY(buttonSpy.wait());
    QSignalSpy moveStartSpy(c, &Toplevel::clientStartUserMovedResized);
    QVERIFY(moveStartSpy.isValid());
    shellSurface->requestMove(Test::get_client().interfaces.seat.get(),
                              buttonSpy.first().first().value<quint32>());
    QVERIFY(moveStartSpy.wait());
    QCOMPARE(win::is_move(c), true);
    QVERIFY(pointerLeftSpy.wait());

    // move a bit
    QSignalSpy clientMoveStepSpy(c, &Toplevel::clientStepUserMovedResized);
    QVERIFY(clientMoveStepSpy.isValid());
    const QPoint startPoint = startGeometry.center();
    const int dragDistance = QApplication::startDragDistance();
    // Why?
    Test::pointer_motion_absolute(startPoint + QPoint(dragDistance, dragDistance) + QPoint(6, 6),
                                  timestamp++);
    QCOMPARE(clientMoveStepSpy.count(), 1);

    // and release again
    Test::pointer_button_released(BTN_LEFT, timestamp++);
    QVERIFY(pointerEnteredSpy.wait());
    QCOMPARE(win::is_move(c), false);
    QCOMPARE(c->frameGeometry(),
             startGeometry.translated(QPoint(dragDistance, dragDistance) + QPoint(6, 6)));
    QCOMPARE(pointerEnteredSpy.last().last().toPoint(), QPoint(49, 24));
}

void MoveResizeWindowTest::testPlasmaShellSurfaceMovable_data()
{
    QTest::addColumn<Wrapland::Client::PlasmaShellSurface::Role>("role");
    QTest::addColumn<bool>("movable");
    QTest::addColumn<bool>("movableAcrossScreens");
    QTest::addColumn<bool>("resizable");

    QTest::newRow("normal") << Wrapland::Client::PlasmaShellSurface::Role::Normal << true << true
                            << true;
    QTest::newRow("desktop") << Wrapland::Client::PlasmaShellSurface::Role::Desktop << false
                             << false << false;
    QTest::newRow("panel") << Wrapland::Client::PlasmaShellSurface::Role::Panel << false << false
                           << false;
    QTest::newRow("osd") << Wrapland::Client::PlasmaShellSurface::Role::OnScreenDisplay << false
                         << false << false;
}

void MoveResizeWindowTest::testPlasmaShellSurfaceMovable()
{
    // this test verifies that certain window types from PlasmaShellSurface are not moveable or
    // resizable
    using namespace Wrapland::Client;
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);

    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    // and a PlasmaShellSurface
    std::unique_ptr<PlasmaShellSurface> plasmaSurface(
        Test::get_client().interfaces.plasma_shell->createSurface(surface.get()));
    QVERIFY(plasmaSurface);
    QFETCH(Wrapland::Client::PlasmaShellSurface::Role, role);
    plasmaSurface->setRole(role);
    // let's render
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

    QVERIFY(c);
    QTEST(c->isMovable(), "movable");
    QTEST(c->isMovableAcrossScreens(), "movableAcrossScreens");
    QTEST(c->isResizable(), "resizable");
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(c));
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

void MoveResizeWindowTest::testNetMove()
{
    // this test verifies that a move request for an X11 window through NET API works
    // create an xcb window
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));

    xcb_window_t w = xcb_generate_id(c.get());
    xcb_create_window(c.get(),
                      XCB_COPY_FROM_PARENT,
                      w,
                      rootWindow(),
                      0,
                      0,
                      100,
                      100,
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      XCB_COPY_FROM_PARENT,
                      0,
                      nullptr);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, 0, 0);
    xcb_icccm_size_hints_set_size(&hints, 1, 100, 100);
    xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
    // let's set a no-border
    NETWinInfo winInfo(c.get(), w, rootWindow(), NET::WMWindowType, NET::Properties2());
    winInfo.setWindowType(NET::Override);
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    QSignalSpy windowCreatedSpy(workspace(), &win::space::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto client = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(client);
    QCOMPARE(client->xcb_window(), w);
    const QRect origGeo = client->frameGeometry();

    // let's move the cursor outside the window
    input::get_cursor()->set_pos(Test::app()->base.screens.geometry(0).center());
    QVERIFY(!origGeo.contains(input::get_cursor()->pos()));

    QSignalSpy moveStartSpy(client, &win::x11::window::clientStartUserMovedResized);
    QVERIFY(moveStartSpy.isValid());
    QSignalSpy moveEndSpy(client, &win::x11::window::clientFinishUserMovedResized);
    QVERIFY(moveEndSpy.isValid());
    QSignalSpy moveStepSpy(client, &win::x11::window::clientStepUserMovedResized);
    QVERIFY(moveStepSpy.isValid());
    QVERIFY(!workspace()->moveResizeClient());

    // use NETRootInfo to trigger a move request
    NETRootInfo root(c.get(), NET::Properties());
    root.moveResizeRequest(w, origGeo.center().x(), origGeo.center().y(), NET::Move);
    xcb_flush(c.get());

    QVERIFY(moveStartSpy.wait());
    QCOMPARE(workspace()->moveResizeClient(), client);
    QVERIFY(win::is_move(client));
    QCOMPARE(client->restore_geometries.maximize, origGeo);
    QCOMPARE(input::get_cursor()->pos(), origGeo.center());

    // let's move a step
    input::get_cursor()->set_pos(input::get_cursor()->pos() + QPoint(10, 10));
    QCOMPARE(moveStepSpy.count(), 1);
    QCOMPARE(moveStepSpy.first().last().toRect(), origGeo.translated(10, 10));

    // let's cancel the move resize again through the net API
    root.moveResizeRequest(w,
                           client->frameGeometry().center().x(),
                           client->frameGeometry().center().y(),
                           NET::MoveResizeCancel);
    xcb_flush(c.get());
    QVERIFY(moveEndSpy.wait());

    // and destroy the window again
    xcb_unmap_window(c.get(), w);
    xcb_destroy_window(c.get(), w);
    xcb_flush(c.get());
    c.reset();

    QSignalSpy windowClosedSpy(client, &win::x11::window::windowClosed);
    QVERIFY(windowClosedSpy.isValid());
    QVERIFY(windowClosedSpy.wait());
}

void MoveResizeWindowTest::testAdjustClientGeometryOfAutohidingX11Panel_data()
{
    QTest::addColumn<QRect>("panelGeometry");
    QTest::addColumn<QPoint>("targetPoint");
    QTest::addColumn<QPoint>("expectedAdjustedPoint");
    QTest::addColumn<quint32>("hideLocation");

    QTest::newRow("top") << QRect(0, 0, 100, 20) << QPoint(50, 25) << QPoint(50, 20) << 0u;
    QTest::newRow("bottom") << QRect(0, 1024 - 20, 100, 20) << QPoint(50, 1024 - 25 - 50)
                            << QPoint(50, 1024 - 20 - 50) << 2u;
    QTest::newRow("left") << QRect(0, 0, 20, 100) << QPoint(25, 50) << QPoint(20, 50) << 3u;
    QTest::newRow("right") << QRect(1280 - 20, 0, 20, 100) << QPoint(1280 - 25 - 100, 50)
                           << QPoint(1280 - 20 - 100, 50) << 1u;
}

void MoveResizeWindowTest::testAdjustClientGeometryOfAutohidingX11Panel()
{
    // this test verifies that auto hiding panels are ignored when adjusting client geometry
    // see BUG 365892

    // first create our panel
    auto c = create_xcb_connection();
    QVERIFY(!xcb_connection_has_error(c.get()));

    xcb_window_t w = xcb_generate_id(c.get());
    QFETCH(QRect, panelGeometry);
    xcb_create_window(c.get(),
                      XCB_COPY_FROM_PARENT,
                      w,
                      rootWindow(),
                      panelGeometry.x(),
                      panelGeometry.y(),
                      panelGeometry.width(),
                      panelGeometry.height(),
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      XCB_COPY_FROM_PARENT,
                      0,
                      nullptr);
    xcb_size_hints_t hints;
    memset(&hints, 0, sizeof(hints));
    xcb_icccm_size_hints_set_position(&hints, 1, panelGeometry.x(), panelGeometry.y());
    xcb_icccm_size_hints_set_size(&hints, 1, panelGeometry.width(), panelGeometry.height());
    xcb_icccm_set_wm_normal_hints(c.get(), w, &hints);
    NETWinInfo winInfo(c.get(), w, rootWindow(), NET::WMWindowType, NET::Properties2());
    winInfo.setWindowType(NET::Dock);
    xcb_map_window(c.get(), w);
    xcb_flush(c.get());

    QSignalSpy windowCreatedSpy(workspace(), &win::space::clientAdded);
    QVERIFY(windowCreatedSpy.isValid());
    QVERIFY(windowCreatedSpy.wait());
    auto panel = windowCreatedSpy.first().first().value<win::x11::window*>();
    QVERIFY(panel);
    QCOMPARE(panel->xcb_window(), w);
    QCOMPARE(panel->frameGeometry(), panelGeometry);
    QVERIFY(win::is_dock(panel));

    // let's create a window
    using namespace Wrapland::Client;
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);

    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    auto testWindow = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

    QVERIFY(testWindow);
    QVERIFY(testWindow->isMovable());
    // panel is not yet hidden, we should snap against it
    QFETCH(QPoint, targetPoint);
    QTEST(workspace()->adjustClientPosition(testWindow, targetPoint, false),
          "expectedAdjustedPoint");

    // now let's hide the panel
    QSignalSpy panelHiddenSpy(panel, &Toplevel::windowHidden);
    QVERIFY(panelHiddenSpy.isValid());
    QFETCH(quint32, hideLocation);
    xcb_change_property(c.get(),
                        XCB_PROP_MODE_REPLACE,
                        w,
                        workspace()->atoms->kde_screen_edge_show,
                        XCB_ATOM_CARDINAL,
                        32,
                        1,
                        &hideLocation);
    xcb_flush(c.get());
    QVERIFY(panelHiddenSpy.wait());

    // now try to snap again
    QCOMPARE(workspace()->adjustClientPosition(testWindow, targetPoint, false), targetPoint);

    // and destroy the panel again
    xcb_unmap_window(c.get(), w);
    xcb_destroy_window(c.get(), w);
    xcb_flush(c.get());
    c.reset();

    QSignalSpy panelClosedSpy(panel, &win::x11::window::windowClosed);
    QVERIFY(panelClosedSpy.isValid());
    QVERIFY(panelClosedSpy.wait());

    // snap once more
    QCOMPARE(workspace()->adjustClientPosition(testWindow, targetPoint, false), targetPoint);

    // and close
    QSignalSpy windowClosedSpy(testWindow, &win::wayland::window::windowClosed);
    QVERIFY(windowClosedSpy.isValid());
    shellSurface.reset();
    surface.reset();
    QVERIFY(windowClosedSpy.wait());
}

void MoveResizeWindowTest::testAdjustClientGeometryOfAutohidingWaylandPanel_data()
{
    QTest::addColumn<QRect>("panelGeometry");
    QTest::addColumn<QPoint>("targetPoint");
    QTest::addColumn<QPoint>("expectedAdjustedPoint");

    QTest::newRow("top") << QRect(0, 0, 100, 20) << QPoint(50, 25) << QPoint(50, 20);
    QTest::newRow("bottom") << QRect(0, 1024 - 20, 100, 20) << QPoint(50, 1024 - 25 - 50)
                            << QPoint(50, 1024 - 20 - 50);
    QTest::newRow("left") << QRect(0, 0, 20, 100) << QPoint(25, 50) << QPoint(20, 50);
    QTest::newRow("right") << QRect(1280 - 20, 0, 20, 100) << QPoint(1280 - 25 - 100, 50)
                           << QPoint(1280 - 20 - 100, 50);
}

void MoveResizeWindowTest::testAdjustClientGeometryOfAutohidingWaylandPanel()
{
    // this test verifies that auto hiding panels are ignored when adjusting client geometry
    // see BUG 365892

    // first create our panel
    using namespace Wrapland::Client;
    std::unique_ptr<Surface> panelSurface(Test::create_surface());
    QVERIFY(panelSurface);
    std::unique_ptr<XdgShellToplevel> panelShellSurface(
        Test::create_xdg_shell_toplevel(panelSurface));
    QVERIFY(panelShellSurface);
    std::unique_ptr<PlasmaShellSurface> plasmaSurface(
        Test::get_client().interfaces.plasma_shell->createSurface(panelSurface.get()));
    QVERIFY(plasmaSurface);
    plasmaSurface->setRole(PlasmaShellSurface::Role::Panel);
    plasmaSurface->setPanelBehavior(PlasmaShellSurface::PanelBehavior::AutoHide);
    QFETCH(QRect, panelGeometry);
    plasmaSurface->setPosition(panelGeometry.topLeft());
    // let's render
    auto panel = Test::render_and_wait_for_shown(panelSurface, panelGeometry.size(), Qt::blue);
    QVERIFY(panel);
    QCOMPARE(panel->frameGeometry(), panelGeometry);
    QVERIFY(win::is_dock(panel));

    // let's create a window
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);

    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    auto testWindow = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);

    QVERIFY(testWindow);
    QVERIFY(testWindow->isMovable());
    // panel is not yet hidden, we should snap against it
    QFETCH(QPoint, targetPoint);
    QTEST(workspace()->adjustClientPosition(testWindow, targetPoint, false),
          "expectedAdjustedPoint");

    // now let's hide the panel
    QSignalSpy panelHiddenSpy(panel, &Toplevel::windowHidden);
    QVERIFY(panelHiddenSpy.isValid());
    plasmaSurface->requestHideAutoHidingPanel();
    QVERIFY(panelHiddenSpy.wait());

    // now try to snap again
    QCOMPARE(workspace()->adjustClientPosition(testWindow, targetPoint, false), targetPoint);

    // and destroy the panel again
    QSignalSpy panelClosedSpy(panel, &win::wayland::window::windowClosed);
    QVERIFY(panelClosedSpy.isValid());
    plasmaSurface.reset();
    panelShellSurface.reset();
    panelSurface.reset();
    QVERIFY(panelClosedSpy.wait());

    // snap once more
    QCOMPARE(workspace()->adjustClientPosition(testWindow, targetPoint, false), targetPoint);

    // and close
    QSignalSpy windowClosedSpy(testWindow, &win::wayland::window::windowClosed);
    QVERIFY(windowClosedSpy.isValid());
    shellSurface.reset();
    surface.reset();
    QVERIFY(windowClosedSpy.wait());
}

void MoveResizeWindowTest::testDestroyMoveClient()
{
    // This test verifies that active move operation gets finished when
    // the associated client is destroyed.

    // Create the test client.
    using namespace Wrapland::Client;
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);

    // Start moving the client.
    QSignalSpy clientStartMoveResizedSpy(client, &Toplevel::clientStartUserMovedResized);
    QVERIFY(clientStartMoveResizedSpy.isValid());
    QSignalSpy clientFinishUserMovedResizedSpy(client, &Toplevel::clientFinishUserMovedResized);
    QVERIFY(clientFinishUserMovedResizedSpy.isValid());

    QCOMPARE(workspace()->moveResizeClient(), nullptr);
    QCOMPARE(win::is_move(client), false);
    QCOMPARE(win::is_resize(client), false);
    workspace()->slotWindowMove();
    QCOMPARE(clientStartMoveResizedSpy.count(), 1);
    QCOMPARE(workspace()->moveResizeClient(), client);
    QCOMPARE(win::is_move(client), true);
    QCOMPARE(win::is_resize(client), false);

    // Let's pretend that the client crashed.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 0);
    QCOMPARE(workspace()->moveResizeClient(), nullptr);
}

void MoveResizeWindowTest::testDestroyResizeClient()
{
    // This test verifies that active resize operation gets finished when
    // the associated client is destroyed.

    // Create the test client.
    using namespace Wrapland::Client;
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);

    // Start resizing the client.
    QSignalSpy clientStartMoveResizedSpy(client, &Toplevel::clientStartUserMovedResized);
    QVERIFY(clientStartMoveResizedSpy.isValid());
    QSignalSpy clientFinishUserMovedResizedSpy(client, &Toplevel::clientFinishUserMovedResized);
    QVERIFY(clientFinishUserMovedResizedSpy.isValid());

    QCOMPARE(workspace()->moveResizeClient(), nullptr);
    QCOMPARE(win::is_move(client), false);
    QCOMPARE(win::is_resize(client), false);
    workspace()->slotWindowResize();
    QCOMPARE(clientStartMoveResizedSpy.count(), 1);
    QCOMPARE(workspace()->moveResizeClient(), client);
    QCOMPARE(win::is_move(client), false);
    QCOMPARE(win::is_resize(client), true);

    // Let's pretend that the client crashed.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 0);
    QCOMPARE(workspace()->moveResizeClient(), nullptr);
}

void MoveResizeWindowTest::testUnmapMoveClient()
{
    // This test verifies that active move operation gets cancelled when
    // the associated client is unmapped.

    // Create the test client.
    using namespace Wrapland::Client;
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);

    // Start resizing the client.
    QSignalSpy clientStartMoveResizedSpy(client, &Toplevel::clientStartUserMovedResized);
    QVERIFY(clientStartMoveResizedSpy.isValid());
    QSignalSpy clientFinishUserMovedResizedSpy(client, &Toplevel::clientFinishUserMovedResized);
    QVERIFY(clientFinishUserMovedResizedSpy.isValid());

    QCOMPARE(workspace()->moveResizeClient(), nullptr);
    QCOMPARE(win::is_move(client), false);
    QCOMPARE(win::is_resize(client), false);
    workspace()->slotWindowMove();
    QCOMPARE(clientStartMoveResizedSpy.count(), 1);
    QCOMPARE(workspace()->moveResizeClient(), client);
    QCOMPARE(win::is_move(client), true);
    QCOMPARE(win::is_resize(client), false);

    // Unmap the client while we're moving it.
    QSignalSpy hiddenSpy(client, &win::wayland::window::windowHidden);
    QVERIFY(hiddenSpy.isValid());
    surface->attachBuffer(Buffer::Ptr());
    surface->commit(Surface::CommitFlag::None);
    QVERIFY(hiddenSpy.wait());
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 0);
    QCOMPARE(workspace()->moveResizeClient(), nullptr);
    QCOMPARE(win::is_move(client), false);
    QCOMPARE(win::is_resize(client), false);

    // Destroy the client.
    shellSurface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 0);
}

void MoveResizeWindowTest::testUnmapResizeClient()
{
    // This test verifies that active resize operation gets cancelled when
    // the associated client is unmapped.

    // Create the test client.
    using namespace Wrapland::Client;
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);

    // Start resizing the client.
    QSignalSpy clientStartMoveResizedSpy(client, &Toplevel::clientStartUserMovedResized);
    QVERIFY(clientStartMoveResizedSpy.isValid());
    QSignalSpy clientFinishUserMovedResizedSpy(client, &Toplevel::clientFinishUserMovedResized);
    QVERIFY(clientFinishUserMovedResizedSpy.isValid());

    QCOMPARE(workspace()->moveResizeClient(), nullptr);
    QCOMPARE(win::is_move(client), false);
    QCOMPARE(win::is_resize(client), false);
    workspace()->slotWindowResize();
    QCOMPARE(clientStartMoveResizedSpy.count(), 1);
    QCOMPARE(workspace()->moveResizeClient(), client);
    QCOMPARE(win::is_move(client), false);
    QCOMPARE(win::is_resize(client), true);

    // Unmap the client while we're resizing it.
    QSignalSpy hiddenSpy(client, &win::wayland::window::windowHidden);
    QVERIFY(hiddenSpy.isValid());
    surface->attachBuffer(Buffer::Ptr());
    surface->commit(Surface::CommitFlag::None);
    QVERIFY(hiddenSpy.wait());
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 0);
    QCOMPARE(workspace()->moveResizeClient(), nullptr);
    QCOMPARE(win::is_move(client), false);
    QCOMPARE(win::is_resize(client), false);

    // Destroy the client.
    shellSurface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
    QCOMPARE(clientFinishUserMovedResizedSpy.count(), 0);
}

void MoveResizeWindowTest::testSetFullScreenWhenMoving()
{
    // Ensure we disable moving event when setFullScreen is triggered
    using namespace Wrapland::Client;

    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);

    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);

    auto client = Test::render_and_wait_for_shown(surface, QSize(500, 800), Qt::blue);
    QVERIFY(client);

    QSignalSpy fullscreen_spy(client, &win::wayland::window::fullScreenChanged);
    QVERIFY(fullscreen_spy.isValid());
    QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    QVERIFY(configureRequestedSpy.wait());

    workspace()->slotWindowMove();
    QCOMPARE(win::is_move(client), true);

    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 2);

    auto states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Fullscreen));

    QCOMPARE(configureRequestedSpy.last().first().toSize(), QSize(500, 800));

    client->setFullScreen(true);

    QCOMPARE(client->control->fullscreen(), false);

    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 3);

    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Fullscreen));

    QCOMPARE(configureRequestedSpy.last().first().toSize(), Test::app()->base.screens.size(0));

    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface, configureRequestedSpy.last().first().toSize(), Qt::red);

    QVERIFY(fullscreen_spy.wait());
    QCOMPARE(fullscreen_spy.size(), 1);

    QCOMPARE(client->control->fullscreen(), true);
    QCOMPARE(win::is_move(client), false);
    QCOMPARE(workspace()->moveResizeClient(), nullptr);

    // Let's pretend that the client crashed.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

void MoveResizeWindowTest::testSetMaximizeWhenMoving()
{
    // Ensure we disable moving event when changeMaximize is triggered
    using namespace Wrapland::Client;

    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);

    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);

    // let's render
    auto client = Test::render_and_wait_for_shown(surface, QSize(500, 800), Qt::blue);
    QVERIFY(client);

    workspace()->slotWindowMove();
    QCOMPARE(win::is_move(client), true);
    win::set_maximize(client, true, true);

    QEXPECT_FAIL("", "The client is still in move state at this point. Is this correct?", Abort);
    QCOMPARE(win::is_move(client), false);
    QCOMPARE(workspace()->moveResizeClient(), nullptr);
    // Let's pretend that the client crashed.
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

}

WAYLANDTEST_MAIN(KWin::MoveResizeWindowTest)

#include "move_resize_window_test.moc"
