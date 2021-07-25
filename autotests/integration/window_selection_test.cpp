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
#include "kwin_wayland_test.h"
#include "cursor.h"
#include "keyboard_input.h"
#include "platform.h"
#include "pointer_input.h"
#include "screens.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/wayland/window.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/keyboard.h>
#include <Wrapland/Client/pointer.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/touch.h>

#include <linux/input.h>

using namespace KWin;
using namespace Wrapland::Client;

static const QString s_socketName = QStringLiteral("wayland_test_kwin_window_selection-0");

class TestWindowSelection : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testSelectOnWindowPointer();
    void testSelectOnWindowKeyboard_data();
    void testSelectOnWindowKeyboard();
    void testSelectOnWindowTouch();
    void testCancelOnWindowPointer();
    void testCancelOnWindowKeyboard();

    void testSelectPointPointer();
    void testSelectPointTouch();
};

void TestWindowSelection::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy workspaceCreatedSpy(kwinApp(), &Application::workspaceCreated);
    QVERIFY(workspaceCreatedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName.toLocal8Bit()));
    qputenv("XKB_DEFAULT_RULES", "evdev");

    kwinApp()->start();
    QMetaObject::invokeMethod(kwinApp()->platform(), "setVirtualOutputs", Qt::DirectConnection, Q_ARG(int, 2));
    QVERIFY(workspaceCreatedSpy.wait());
    QCOMPARE(screens()->count(), 2);
    QCOMPARE(screens()->geometry(0), QRect(0, 0, 1280, 1024));
    QCOMPARE(screens()->geometry(1), QRect(1280, 0, 1280, 1024));
    waylandServer()->initWorkspace();
}

void TestWindowSelection::init()
{
    Test::setup_wayland_connection(Test::AdditionalWaylandInterface::Seat);
    QVERIFY(Test::wait_for_wayland_pointer());

    screens()->setCurrent(0);
    KWin::Cursor::setPos(QPoint(1280, 512));
}

void TestWindowSelection::cleanup()
{
    Test::destroy_wayland_connection();
}

void TestWindowSelection::testSelectOnWindowPointer()
{
    // this test verifies window selection through pointer works
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    std::unique_ptr<Pointer> pointer(Test::get_client().interfaces.seat->createPointer());
    std::unique_ptr<Keyboard> keyboard(Test::get_client().interfaces.seat->createKeyboard());
    QSignalSpy pointerEnteredSpy(pointer.get(), &Pointer::entered);
    QVERIFY(pointerEnteredSpy.isValid());
    QSignalSpy pointerLeftSpy(pointer.get(), &Pointer::left);
    QVERIFY(pointerLeftSpy.isValid());
    QSignalSpy keyboardEnteredSpy(keyboard.get(), &Keyboard::entered);
    QVERIFY(keyboardEnteredSpy.isValid());
    QSignalSpy keyboardLeftSpy(keyboard.get(), &Keyboard::left);
    QVERIFY(keyboardLeftSpy.isValid());

    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QVERIFY(keyboardEnteredSpy.wait());
    KWin::Cursor::setPos(client->frameGeometry().center());
    QCOMPARE(input_redirect()->pointer()->focus(), client);
    QVERIFY(pointerEnteredSpy.wait());

    Toplevel *selectedWindow = nullptr;
    auto callback = [&selectedWindow] (Toplevel *t) {
        selectedWindow = t;
    };

    // start the interaction
    QCOMPARE(input_redirect()->isSelectingWindow(), false);
    kwinApp()->platform()->startInteractiveWindowSelection(callback);
    QCOMPARE(input_redirect()->isSelectingWindow(), true);
    QVERIFY(!selectedWindow);
    QCOMPARE(keyboardLeftSpy.count(), 0);
    QVERIFY(pointerLeftSpy.wait());
    if (keyboardLeftSpy.isEmpty()) {
        QVERIFY(keyboardLeftSpy.wait());
    }
    QCOMPARE(pointerLeftSpy.count(), 1);
    QCOMPARE(keyboardLeftSpy.count(), 1);

    // simulate left button press
    quint32 timestamp = 0;
    Test::pointer_button_pressed(BTN_LEFT, timestamp++);
    // should not have ended the mode
    QCOMPARE(input_redirect()->isSelectingWindow(), true);
    QVERIFY(!selectedWindow);
    QVERIFY(!input_redirect()->pointer()->focus());

    // updating the pointer should not change anything
    input_redirect()->pointer()->update();
    QVERIFY(!input_redirect()->pointer()->focus());
    // updating keyboard should also not change
    input_redirect()->keyboard()->update();

    // perform a right button click
    Test::pointer_button_pressed(BTN_RIGHT, timestamp++);
    Test::pointer_button_released(BTN_RIGHT, timestamp++);
    // should not have ended the mode
    QCOMPARE(input_redirect()->isSelectingWindow(), true);
    QVERIFY(!selectedWindow);
    // now release
    Test::pointer_button_released(BTN_LEFT, timestamp++);
    QCOMPARE(input_redirect()->isSelectingWindow(), false);
    QCOMPARE(selectedWindow, client);
    QCOMPARE(input_redirect()->pointer()->focus(), client);
    // should give back keyboard and pointer
    QVERIFY(pointerEnteredSpy.wait());
    if (keyboardEnteredSpy.count() != 2) {
        QVERIFY(keyboardEnteredSpy.wait());
    }
    QCOMPARE(pointerLeftSpy.count(), 1);
    QCOMPARE(keyboardLeftSpy.count(), 1);
    QCOMPARE(pointerEnteredSpy.count(), 2);
    QCOMPARE(keyboardEnteredSpy.count(), 2);
}

void TestWindowSelection::testSelectOnWindowKeyboard_data()
{
    QTest::addColumn<qint32>("key");

    QTest::newRow("enter") << KEY_ENTER;
    QTest::newRow("keypad enter") << KEY_KPENTER;
    QTest::newRow("space") << KEY_SPACE;
}

void TestWindowSelection::testSelectOnWindowKeyboard()
{
    // this test verifies window selection through keyboard key
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    std::unique_ptr<Pointer> pointer(Test::get_client().interfaces.seat->createPointer());
    std::unique_ptr<Keyboard> keyboard(Test::get_client().interfaces.seat->createKeyboard());
    QSignalSpy pointerEnteredSpy(pointer.get(), &Pointer::entered);
    QVERIFY(pointerEnteredSpy.isValid());
    QSignalSpy pointerLeftSpy(pointer.get(), &Pointer::left);
    QVERIFY(pointerLeftSpy.isValid());
    QSignalSpy keyboardEnteredSpy(keyboard.get(), &Keyboard::entered);
    QVERIFY(keyboardEnteredSpy.isValid());
    QSignalSpy keyboardLeftSpy(keyboard.get(), &Keyboard::left);
    QVERIFY(keyboardLeftSpy.isValid());

    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QVERIFY(keyboardEnteredSpy.wait());
    QVERIFY(!client->frameGeometry().contains(KWin::Cursor::pos()));

    Toplevel *selectedWindow = nullptr;
    auto callback = [&selectedWindow] (Toplevel *t) {
        selectedWindow = t;
    };

    // start the interaction
    QCOMPARE(input_redirect()->isSelectingWindow(), false);
    kwinApp()->platform()->startInteractiveWindowSelection(callback);
    QCOMPARE(input_redirect()->isSelectingWindow(), true);
    QVERIFY(!selectedWindow);
    QCOMPARE(keyboardLeftSpy.count(), 0);
    QVERIFY(keyboardLeftSpy.wait());
    QCOMPARE(pointerLeftSpy.count(), 0);
    QCOMPARE(keyboardLeftSpy.count(), 1);

    // simulate key press
    quint32 timestamp = 0;
    // move cursor through keys
    auto keyPress = [&timestamp] (qint32 key) {
        Test::keyboard_key_pressed(key, timestamp++);
        Test::keyboard_key_released(key, timestamp++);
    };
    while (KWin::Cursor::pos().x() >= client->frameGeometry().x() + client->frameGeometry().width()) {
        keyPress(KEY_LEFT);
    }
    while (KWin::Cursor::pos().x() <= client->frameGeometry().x()) {
        keyPress(KEY_RIGHT);
    }
    while (KWin::Cursor::pos().y() <= client->frameGeometry().y()) {
        keyPress(KEY_DOWN);
    }
    while (KWin::Cursor::pos().y() >= client->frameGeometry().y() + client->frameGeometry().height()) {
        keyPress(KEY_UP);
    }
    QFETCH(qint32, key);
    Test::keyboard_key_pressed(key, timestamp++);
    QCOMPARE(input_redirect()->isSelectingWindow(), false);
    QCOMPARE(selectedWindow, client);
    QCOMPARE(input_redirect()->pointer()->focus(), client);
    // should give back keyboard and pointer
    QVERIFY(pointerEnteredSpy.wait());
    if (keyboardEnteredSpy.count() != 2) {
        QVERIFY(keyboardEnteredSpy.wait());
    }
    QCOMPARE(pointerLeftSpy.count(), 0);
    QCOMPARE(keyboardLeftSpy.count(), 1);
    QCOMPARE(pointerEnteredSpy.count(), 1);
    QCOMPARE(keyboardEnteredSpy.count(), 2);
    Test::keyboard_key_released(key, timestamp++);
}

void TestWindowSelection::testSelectOnWindowTouch()
{
    // this test verifies window selection through touch
    std::unique_ptr<Touch> touch(Test::get_client().interfaces.seat->createTouch());
    QSignalSpy touchStartedSpy(touch.get(), &Touch::sequenceStarted);
    QVERIFY(touchStartedSpy.isValid());
    QSignalSpy touchCanceledSpy(touch.get(), &Touch::sequenceCanceled);
    QVERIFY(touchCanceledSpy.isValid());
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);

    Toplevel *selectedWindow = nullptr;
    auto callback = [&selectedWindow] (Toplevel *t) {
        selectedWindow = t;
    };

    // start the interaction
    QCOMPARE(input_redirect()->isSelectingWindow(), false);
    kwinApp()->platform()->startInteractiveWindowSelection(callback);
    QCOMPARE(input_redirect()->isSelectingWindow(), true);
    QVERIFY(!selectedWindow);

    // simulate touch down
    quint32 timestamp = 0;
    Test::touch_down(0, client->frameGeometry().center(), timestamp++);
    QVERIFY(!selectedWindow);
    Test::touch_up(0, timestamp++);
    QCOMPARE(input_redirect()->isSelectingWindow(), false);
    QCOMPARE(selectedWindow, client);

    // with movement
    selectedWindow = nullptr;
    kwinApp()->platform()->startInteractiveWindowSelection(callback);
    Test::touch_down(0, client->frameGeometry().bottomRight() + QPoint(20, 20), timestamp++);
    QVERIFY(!selectedWindow);
    Test::touch_motion(0, client->frameGeometry().bottomRight() - QPoint(1, 1), timestamp++);
    QVERIFY(!selectedWindow);
    Test::touch_up(0, timestamp++);
    QCOMPARE(selectedWindow, client);
    QCOMPARE(input_redirect()->isSelectingWindow(), false);

    // it cancels active touch sequence on the window
    Test::touch_down(0, client->frameGeometry().center(), timestamp++);
    QVERIFY(touchStartedSpy.wait());
    selectedWindow = nullptr;
    kwinApp()->platform()->startInteractiveWindowSelection(callback);
    QCOMPARE(input_redirect()->isSelectingWindow(), true);
    QVERIFY(touchCanceledSpy.wait());
    QVERIFY(!selectedWindow);
    // this touch up does not yet select the window, it was started prior to the selection
    Test::touch_up(0, timestamp++);
    QVERIFY(!selectedWindow);
    Test::touch_down(0, client->frameGeometry().center(), timestamp++);
    Test::touch_up(0, timestamp++);
    QCOMPARE(selectedWindow, client);
    QCOMPARE(input_redirect()->isSelectingWindow(), false);

    QCOMPARE(touchStartedSpy.count(), 1);
    QCOMPARE(touchCanceledSpy.count(), 1);
}

void TestWindowSelection::testCancelOnWindowPointer()
{
    // this test verifies that window selection cancels through right button click
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    std::unique_ptr<Pointer> pointer(Test::get_client().interfaces.seat->createPointer());
    std::unique_ptr<Keyboard> keyboard(Test::get_client().interfaces.seat->createKeyboard());
    QSignalSpy pointerEnteredSpy(pointer.get(), &Pointer::entered);
    QVERIFY(pointerEnteredSpy.isValid());
    QSignalSpy pointerLeftSpy(pointer.get(), &Pointer::left);
    QVERIFY(pointerLeftSpy.isValid());
    QSignalSpy keyboardEnteredSpy(keyboard.get(), &Keyboard::entered);
    QVERIFY(keyboardEnteredSpy.isValid());
    QSignalSpy keyboardLeftSpy(keyboard.get(), &Keyboard::left);
    QVERIFY(keyboardLeftSpy.isValid());

    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QVERIFY(keyboardEnteredSpy.wait());
    KWin::Cursor::setPos(client->frameGeometry().center());
    QCOMPARE(input_redirect()->pointer()->focus(), client);
    QVERIFY(pointerEnteredSpy.wait());

    Toplevel *selectedWindow = nullptr;
    auto callback = [&selectedWindow] (Toplevel *t) {
        selectedWindow = t;
    };

    // start the interaction
    QCOMPARE(input_redirect()->isSelectingWindow(), false);
    kwinApp()->platform()->startInteractiveWindowSelection(callback);
    QCOMPARE(input_redirect()->isSelectingWindow(), true);
    QVERIFY(!selectedWindow);
    QCOMPARE(keyboardLeftSpy.count(), 0);
    QVERIFY(pointerLeftSpy.wait());
    if (keyboardLeftSpy.isEmpty()) {
        QVERIFY(keyboardLeftSpy.wait());
    }
    QCOMPARE(pointerLeftSpy.count(), 1);
    QCOMPARE(keyboardLeftSpy.count(), 1);

    // simulate left button press
    quint32 timestamp = 0;
    Test::pointer_button_pressed(BTN_RIGHT, timestamp++);
    Test::pointer_button_released(BTN_RIGHT, timestamp++);
    QCOMPARE(input_redirect()->isSelectingWindow(), false);
    QVERIFY(!selectedWindow);
    QCOMPARE(input_redirect()->pointer()->focus(), client);
    // should give back keyboard and pointer
    QVERIFY(pointerEnteredSpy.wait());
    if (keyboardEnteredSpy.count() != 2) {
        QVERIFY(keyboardEnteredSpy.wait());
    }
    QCOMPARE(pointerLeftSpy.count(), 1);
    QCOMPARE(keyboardLeftSpy.count(), 1);
    QCOMPARE(pointerEnteredSpy.count(), 2);
    QCOMPARE(keyboardEnteredSpy.count(), 2);
}

void TestWindowSelection::testCancelOnWindowKeyboard()
{
    // this test verifies that cancel window selection through escape key works
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    std::unique_ptr<Pointer> pointer(Test::get_client().interfaces.seat->createPointer());
    std::unique_ptr<Keyboard> keyboard(Test::get_client().interfaces.seat->createKeyboard());
    QSignalSpy pointerEnteredSpy(pointer.get(), &Pointer::entered);
    QVERIFY(pointerEnteredSpy.isValid());
    QSignalSpy pointerLeftSpy(pointer.get(), &Pointer::left);
    QVERIFY(pointerLeftSpy.isValid());
    QSignalSpy keyboardEnteredSpy(keyboard.get(), &Keyboard::entered);
    QVERIFY(keyboardEnteredSpy.isValid());
    QSignalSpy keyboardLeftSpy(keyboard.get(), &Keyboard::left);
    QVERIFY(keyboardLeftSpy.isValid());

    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QVERIFY(keyboardEnteredSpy.wait());
    KWin::Cursor::setPos(client->frameGeometry().center());
    QCOMPARE(input_redirect()->pointer()->focus(), client);
    QVERIFY(pointerEnteredSpy.wait());

    Toplevel *selectedWindow = nullptr;
    auto callback = [&selectedWindow] (Toplevel *t) {
        selectedWindow = t;
    };

    // start the interaction
    QCOMPARE(input_redirect()->isSelectingWindow(), false);
    kwinApp()->platform()->startInteractiveWindowSelection(callback);
    QCOMPARE(input_redirect()->isSelectingWindow(), true);
    QVERIFY(!selectedWindow);
    QCOMPARE(keyboardLeftSpy.count(), 0);
    QVERIFY(pointerLeftSpy.wait());
    if (keyboardLeftSpy.isEmpty()) {
        QVERIFY(keyboardLeftSpy.wait());
    }
    QCOMPARE(pointerLeftSpy.count(), 1);
    QCOMPARE(keyboardLeftSpy.count(), 1);

    // simulate left button press
    quint32 timestamp = 0;
    Test::keyboard_key_pressed(KEY_ESC, timestamp++);
    QCOMPARE(input_redirect()->isSelectingWindow(), false);
    QVERIFY(!selectedWindow);
    QCOMPARE(input_redirect()->pointer()->focus(), client);
    // should give back keyboard and pointer
    QVERIFY(pointerEnteredSpy.wait());
    if (keyboardEnteredSpy.count() != 2) {
        QVERIFY(keyboardEnteredSpy.wait());
    }
    QCOMPARE(pointerLeftSpy.count(), 1);
    QCOMPARE(keyboardLeftSpy.count(), 1);
    QCOMPARE(pointerEnteredSpy.count(), 2);
    QCOMPARE(keyboardEnteredSpy.count(), 2);
    Test::keyboard_key_released(KEY_ESC, timestamp++);
}

void TestWindowSelection::testSelectPointPointer()
{
    // this test verifies point selection through pointer works
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    std::unique_ptr<Pointer> pointer(Test::get_client().interfaces.seat->createPointer());
    std::unique_ptr<Keyboard> keyboard(Test::get_client().interfaces.seat->createKeyboard());
    QSignalSpy pointerEnteredSpy(pointer.get(), &Pointer::entered);
    QVERIFY(pointerEnteredSpy.isValid());
    QSignalSpy pointerLeftSpy(pointer.get(), &Pointer::left);
    QVERIFY(pointerLeftSpy.isValid());
    QSignalSpy keyboardEnteredSpy(keyboard.get(), &Keyboard::entered);
    QVERIFY(keyboardEnteredSpy.isValid());
    QSignalSpy keyboardLeftSpy(keyboard.get(), &Keyboard::left);
    QVERIFY(keyboardLeftSpy.isValid());

    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QVERIFY(keyboardEnteredSpy.wait());
    KWin::Cursor::setPos(client->frameGeometry().center());
    QCOMPARE(input_redirect()->pointer()->focus(), client);
    QVERIFY(pointerEnteredSpy.wait());

    QPoint point;
    auto callback = [&point] (const QPoint &p) {
        point = p;
    };

    // start the interaction
    QCOMPARE(input_redirect()->isSelectingWindow(), false);
    kwinApp()->platform()->startInteractivePositionSelection(callback);
    QCOMPARE(input_redirect()->isSelectingWindow(), true);
    QCOMPARE(point, QPoint());
    QCOMPARE(keyboardLeftSpy.count(), 0);
    QVERIFY(pointerLeftSpy.wait());
    if (keyboardLeftSpy.isEmpty()) {
        QVERIFY(keyboardLeftSpy.wait());
    }
    QCOMPARE(pointerLeftSpy.count(), 1);
    QCOMPARE(keyboardLeftSpy.count(), 1);

    // trying again should not be allowed
    QPoint point2;
    kwinApp()->platform()->startInteractivePositionSelection([&point2] (const QPoint &p) {
        point2 = p;
    });
    QCOMPARE(point2, QPoint(-1, -1));

    // simulate left button press
    quint32 timestamp = 0;
    Test::pointer_button_pressed(BTN_LEFT, timestamp++);
    // should not have ended the mode
    QCOMPARE(input_redirect()->isSelectingWindow(), true);
    QCOMPARE(point, QPoint());
    QVERIFY(!input_redirect()->pointer()->focus());

    // updating the pointer should not change anything
    input_redirect()->pointer()->update();
    QVERIFY(!input_redirect()->pointer()->focus());
    // updating keyboard should also not change
    input_redirect()->keyboard()->update();

    // perform a right button click
    Test::pointer_button_pressed(BTN_RIGHT, timestamp++);
    Test::pointer_button_released(BTN_RIGHT, timestamp++);
    // should not have ended the mode
    QCOMPARE(input_redirect()->isSelectingWindow(), true);
    QCOMPARE(point, QPoint());
    // now release
    Test::pointer_button_released(BTN_LEFT, timestamp++);
    QCOMPARE(input_redirect()->isSelectingWindow(), false);
    QCOMPARE(point, input_redirect()->globalPointer().toPoint());
    QCOMPARE(input_redirect()->pointer()->focus(), client);
    // should give back keyboard and pointer
    QVERIFY(pointerEnteredSpy.wait());
    if (keyboardEnteredSpy.count() != 2) {
        QVERIFY(keyboardEnteredSpy.wait());
    }
    QCOMPARE(pointerLeftSpy.count(), 1);
    QCOMPARE(keyboardLeftSpy.count(), 1);
    QCOMPARE(pointerEnteredSpy.count(), 2);
    QCOMPARE(keyboardEnteredSpy.count(), 2);
}

void TestWindowSelection::testSelectPointTouch()
{
    // this test verifies point selection through touch works
    QPoint point;
    auto callback = [&point] (const QPoint &p) {
        point = p;
    };

    // start the interaction
    QCOMPARE(input_redirect()->isSelectingWindow(), false);
    kwinApp()->platform()->startInteractivePositionSelection(callback);
    QCOMPARE(input_redirect()->isSelectingWindow(), true);
    QCOMPARE(point, QPoint());

    // let's create multiple touch points
    quint32 timestamp = 0;
    Test::touch_down(0, QPointF(0, 1), timestamp++);
    QCOMPARE(input_redirect()->isSelectingWindow(), true);
    Test::touch_down(1, QPointF(10, 20), timestamp++);
    QCOMPARE(input_redirect()->isSelectingWindow(), true);
    Test::touch_down(2, QPointF(30, 40), timestamp++);
    QCOMPARE(input_redirect()->isSelectingWindow(), true);

    // let's move our points
    Test::touch_motion(0, QPointF(5, 10), timestamp++);
    Test::touch_motion(2, QPointF(20, 25), timestamp++);
    Test::touch_motion(1, QPointF(25, 35), timestamp++);
    QCOMPARE(input_redirect()->isSelectingWindow(), true);
    Test::touch_up(0, timestamp++);
    QCOMPARE(input_redirect()->isSelectingWindow(), true);
    Test::touch_up(2, timestamp++);
    QCOMPARE(input_redirect()->isSelectingWindow(), true);
    Test::touch_up(1, timestamp++);
    QCOMPARE(input_redirect()->isSelectingWindow(), false);
    QCOMPARE(point, QPoint(25, 35));
}

WAYLANDTEST_MAIN(TestWindowSelection)
#include "window_selection_test.moc"
