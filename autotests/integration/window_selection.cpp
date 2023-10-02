/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "input/keyboard_redirect.h"
#include "input/pointer_redirect.h"
#include "input/wayland/device_redirect.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/keyboard.h>
#include <Wrapland/Client/pointer.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/touch.h>

#include <catch2/generators/catch_generators.hpp>
#include <linux/input.h>

using namespace Wrapland::Client;

namespace KWin::detail::test
{

TEST_CASE("window selection", "[win]")
{
    qputenv("XKB_DEFAULT_RULES", "evdev");

    auto operation_mode = GENERATE(base::operation_mode::wayland, base::operation_mode::xwayland);
    test::setup setup("window-selection", operation_mode);
    setup.start();
    setup.set_outputs(2);
    test_outputs_default();
    cursor()->set_pos(QPoint(1280, 512));

    setup_wayland_connection(global_selection::seat);
    QVERIFY(wait_for_wayland_pointer());

    SECTION("select on window pointer")
    {
        // this test verifies window selection through pointer works
        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        std::unique_ptr<Pointer> pointer(get_client().interfaces.seat->createPointer());
        std::unique_ptr<Keyboard> keyboard(get_client().interfaces.seat->createKeyboard());
        QSignalSpy pointerEnteredSpy(pointer.get(), &Pointer::entered);
        QVERIFY(pointerEnteredSpy.isValid());
        QSignalSpy pointerLeftSpy(pointer.get(), &Pointer::left);
        QVERIFY(pointerLeftSpy.isValid());
        QSignalSpy keyboardEnteredSpy(keyboard.get(), &Keyboard::entered);
        QVERIFY(keyboardEnteredSpy.isValid());
        QSignalSpy keyboardLeftSpy(keyboard.get(), &Keyboard::left);
        QVERIFY(keyboardLeftSpy.isValid());

        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);
        QVERIFY(keyboardEnteredSpy.wait());
        cursor()->set_pos(client->geo.frame.center());
        QCOMPARE(get_wayland_window(setup.base->space->input->pointer->focus.window), client);
        QVERIFY(pointerEnteredSpy.wait());

        std::optional<space::window_t> selectedWindow;
        auto callback = [&selectedWindow](std::optional<space::window_t> t) { selectedWindow = t; };

        // start the interaction
        QCOMPARE(setup.base->space->input->isSelectingWindow(), false);
        setup.base->space->input->start_interactive_window_selection(callback);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), true);
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
        pointer_button_pressed(BTN_LEFT, timestamp++);
        // should not have ended the mode
        QCOMPARE(setup.base->space->input->isSelectingWindow(), true);
        QVERIFY(!selectedWindow);
        QVERIFY(!setup.base->space->input->pointer->focus.window);

        // updating the pointer should not change anything
        input::wayland::device_redirect_update(setup.base->space->input->pointer.get());
        QVERIFY(!setup.base->space->input->pointer->focus.window);
        // updating keyboard should also not change
        setup.base->space->input->keyboard->update();

        // perform a right button click
        pointer_button_pressed(BTN_RIGHT, timestamp++);
        pointer_button_released(BTN_RIGHT, timestamp++);
        // should not have ended the mode
        QCOMPARE(setup.base->space->input->isSelectingWindow(), true);
        QVERIFY(!selectedWindow);
        // now release
        pointer_button_released(BTN_LEFT, timestamp++);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), false);
        QCOMPARE(get_wayland_window(selectedWindow), client);
        QCOMPARE(get_wayland_window(setup.base->space->input->pointer->focus.window), client);
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

    SECTION("select on window keyboard")
    {
        // this test verifies window selection through keyboard key

        auto key = GENERATE(KEY_ENTER, KEY_KPENTER, KEY_SPACE);

        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        std::unique_ptr<Pointer> pointer(get_client().interfaces.seat->createPointer());
        std::unique_ptr<Keyboard> keyboard(get_client().interfaces.seat->createKeyboard());

        QSignalSpy pointerEnteredSpy(pointer.get(), &Pointer::entered);
        QVERIFY(pointerEnteredSpy.isValid());
        QSignalSpy pointerLeftSpy(pointer.get(), &Pointer::left);
        QVERIFY(pointerLeftSpy.isValid());
        QSignalSpy keyboardEnteredSpy(keyboard.get(), &Keyboard::entered);
        QVERIFY(keyboardEnteredSpy.isValid());
        QSignalSpy keyboardLeftSpy(keyboard.get(), &Keyboard::left);
        QVERIFY(keyboardLeftSpy.isValid());

        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);
        QVERIFY(keyboardEnteredSpy.wait());
        QVERIFY(!client->geo.frame.contains(cursor()->pos()));

        std::optional<space::window_t> selectedWindow;
        auto callback = [&selectedWindow](std::optional<space::window_t> t) { selectedWindow = t; };

        // start the interaction
        QCOMPARE(setup.base->space->input->isSelectingWindow(), false);
        setup.base->space->input->start_interactive_window_selection(callback);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), true);
        QVERIFY(!selectedWindow);
        QCOMPARE(keyboardLeftSpy.count(), 0);
        QVERIFY(keyboardLeftSpy.wait());
        QCOMPARE(pointerLeftSpy.count(), 0);
        QCOMPARE(keyboardLeftSpy.count(), 1);

        // simulate key press
        quint32 timestamp = 0;

        // move cursor through keys
        auto keyPress = [&timestamp](qint32 key) {
            keyboard_key_pressed(key, timestamp++);
            keyboard_key_released(key, timestamp++);
        };
        while (cursor()->pos().x() >= client->geo.frame.x() + client->geo.frame.width()) {
            keyPress(KEY_LEFT);
        }
        while (cursor()->pos().x() <= client->geo.frame.x()) {
            keyPress(KEY_RIGHT);
        }
        while (cursor()->pos().y() <= client->geo.frame.y()) {
            keyPress(KEY_DOWN);
        }
        while (cursor()->pos().y() >= client->geo.frame.y() + client->geo.frame.height()) {
            keyPress(KEY_UP);
        }

        keyboard_key_pressed(key, timestamp++);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), false);
        QVERIFY(selectedWindow);
        QCOMPARE(*selectedWindow, space::window_t(client));
        QCOMPARE(get_wayland_window(setup.base->space->input->pointer->focus.window), client);

        // should give back keyboard and pointer
        QVERIFY(pointerEnteredSpy.wait());
        if (keyboardEnteredSpy.count() != 2) {
            QVERIFY(keyboardEnteredSpy.wait());
        }
        QCOMPARE(pointerLeftSpy.count(), 0);
        QCOMPARE(keyboardLeftSpy.count(), 1);
        QCOMPARE(pointerEnteredSpy.count(), 1);
        QCOMPARE(keyboardEnteredSpy.count(), 2);
        keyboard_key_released(key, timestamp++);
    }

    SECTION("select on window touch")
    {
        // this test verifies window selection through touch
        std::unique_ptr<Touch> touch(get_client().interfaces.seat->createTouch());
        QSignalSpy touchStartedSpy(touch.get(), &Touch::sequenceStarted);
        QVERIFY(touchStartedSpy.isValid());
        QSignalSpy touchCanceledSpy(touch.get(), &Touch::sequenceCanceled);
        QVERIFY(touchCanceledSpy.isValid());
        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);

        std::optional<space::window_t> selectedWindow;
        auto callback = [&selectedWindow](std::optional<space::window_t> t) { selectedWindow = t; };

        // start the interaction
        QCOMPARE(setup.base->space->input->isSelectingWindow(), false);
        setup.base->space->input->start_interactive_window_selection(callback);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), true);
        QVERIFY(!selectedWindow);

        // simulate touch down
        quint32 timestamp = 0;
        touch_down(0, client->geo.frame.center(), timestamp++);
        QVERIFY(!selectedWindow);
        touch_up(0, timestamp++);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), false);
        QVERIFY(selectedWindow);
        QCOMPARE(*selectedWindow, space::window_t(client));

        // with movement
        selectedWindow = {};
        setup.base->space->input->start_interactive_window_selection(callback);
        touch_down(0, client->geo.frame.bottomRight() + QPoint(20, 20), timestamp++);
        QVERIFY(!selectedWindow);
        touch_motion(0, client->geo.frame.bottomRight() - QPoint(1, 1), timestamp++);
        QVERIFY(!selectedWindow);
        touch_up(0, timestamp++);
        QVERIFY(selectedWindow);
        QCOMPARE(get_wayland_window(selectedWindow), client);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), false);

        // it cancels active touch sequence on the window
        touch_down(0, client->geo.frame.center(), timestamp++);
        QVERIFY(touchStartedSpy.wait());
        selectedWindow = {};
        setup.base->space->input->start_interactive_window_selection(callback);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), true);
        QVERIFY(touchCanceledSpy.wait());
        QVERIFY(!selectedWindow);
        // this touch up does not yet select the window, it was started prior to the selection
        touch_up(0, timestamp++);
        QVERIFY(!selectedWindow);
        touch_down(0, client->geo.frame.center(), timestamp++);
        touch_up(0, timestamp++);
        QCOMPARE(get_wayland_window(selectedWindow), client);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), false);

        QCOMPARE(touchStartedSpy.count(), 1);
        QCOMPARE(touchCanceledSpy.count(), 1);
    }

    SECTION("cancel on window pointer")
    {
        // this test verifies that window selection cancels through right button click
        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        std::unique_ptr<Pointer> pointer(get_client().interfaces.seat->createPointer());
        std::unique_ptr<Keyboard> keyboard(get_client().interfaces.seat->createKeyboard());
        QSignalSpy pointerEnteredSpy(pointer.get(), &Pointer::entered);
        QVERIFY(pointerEnteredSpy.isValid());
        QSignalSpy pointerLeftSpy(pointer.get(), &Pointer::left);
        QVERIFY(pointerLeftSpy.isValid());
        QSignalSpy keyboardEnteredSpy(keyboard.get(), &Keyboard::entered);
        QVERIFY(keyboardEnteredSpy.isValid());
        QSignalSpy keyboardLeftSpy(keyboard.get(), &Keyboard::left);
        QVERIFY(keyboardLeftSpy.isValid());

        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);
        QVERIFY(keyboardEnteredSpy.wait());
        cursor()->set_pos(client->geo.frame.center());
        QCOMPARE(get_wayland_window(setup.base->space->input->pointer->focus.window), client);
        QVERIFY(pointerEnteredSpy.wait());

        std::optional<space::window_t> selectedWindow;
        auto callback = [&selectedWindow](std::optional<space::window_t> t) { selectedWindow = t; };

        // start the interaction
        QCOMPARE(setup.base->space->input->isSelectingWindow(), false);
        setup.base->space->input->start_interactive_window_selection(callback);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), true);
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
        pointer_button_pressed(BTN_RIGHT, timestamp++);
        pointer_button_released(BTN_RIGHT, timestamp++);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), false);
        QVERIFY(!selectedWindow);
        QCOMPARE(get_wayland_window(setup.base->space->input->pointer->focus.window), client);
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

    SECTION("cancel on window keyboard")
    {
        // this test verifies that cancel window selection through escape key works
        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        std::unique_ptr<Pointer> pointer(get_client().interfaces.seat->createPointer());
        std::unique_ptr<Keyboard> keyboard(get_client().interfaces.seat->createKeyboard());
        QSignalSpy pointerEnteredSpy(pointer.get(), &Pointer::entered);
        QVERIFY(pointerEnteredSpy.isValid());
        QSignalSpy pointerLeftSpy(pointer.get(), &Pointer::left);
        QVERIFY(pointerLeftSpy.isValid());
        QSignalSpy keyboardEnteredSpy(keyboard.get(), &Keyboard::entered);
        QVERIFY(keyboardEnteredSpy.isValid());
        QSignalSpy keyboardLeftSpy(keyboard.get(), &Keyboard::left);
        QVERIFY(keyboardLeftSpy.isValid());

        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);
        QVERIFY(keyboardEnteredSpy.wait());
        cursor()->set_pos(client->geo.frame.center());
        QCOMPARE(get_wayland_window(setup.base->space->input->pointer->focus.window), client);
        QVERIFY(pointerEnteredSpy.wait());

        std::optional<space::window_t> selectedWindow;
        auto callback = [&selectedWindow](std::optional<space::window_t> t) { selectedWindow = t; };

        // start the interaction
        QCOMPARE(setup.base->space->input->isSelectingWindow(), false);
        setup.base->space->input->start_interactive_window_selection(callback);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), true);
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
        keyboard_key_pressed(KEY_ESC, timestamp++);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), false);
        QVERIFY(!selectedWindow);
        QCOMPARE(get_wayland_window(setup.base->space->input->pointer->focus.window), client);
        // should give back keyboard and pointer
        QVERIFY(pointerEnteredSpy.wait());
        if (keyboardEnteredSpy.count() != 2) {
            QVERIFY(keyboardEnteredSpy.wait());
        }
        QCOMPARE(pointerLeftSpy.count(), 1);
        QCOMPARE(keyboardLeftSpy.count(), 1);
        QCOMPARE(pointerEnteredSpy.count(), 2);
        QCOMPARE(keyboardEnteredSpy.count(), 2);
        keyboard_key_released(KEY_ESC, timestamp++);
    }

    SECTION("select point pointer")
    {
        // this test verifies point selection through pointer works
        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        std::unique_ptr<Pointer> pointer(get_client().interfaces.seat->createPointer());
        std::unique_ptr<Keyboard> keyboard(get_client().interfaces.seat->createKeyboard());
        QSignalSpy pointerEnteredSpy(pointer.get(), &Pointer::entered);
        QVERIFY(pointerEnteredSpy.isValid());
        QSignalSpy pointerLeftSpy(pointer.get(), &Pointer::left);
        QVERIFY(pointerLeftSpy.isValid());
        QSignalSpy keyboardEnteredSpy(keyboard.get(), &Keyboard::entered);
        QVERIFY(keyboardEnteredSpy.isValid());
        QSignalSpy keyboardLeftSpy(keyboard.get(), &Keyboard::left);
        QVERIFY(keyboardLeftSpy.isValid());

        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);
        QVERIFY(keyboardEnteredSpy.wait());
        cursor()->set_pos(client->geo.frame.center());
        QCOMPARE(get_wayland_window(setup.base->space->input->pointer->focus.window), client);
        QVERIFY(pointerEnteredSpy.wait());

        QPoint point;
        auto callback = [&point](const QPoint& p) { point = p; };

        // start the interaction
        QCOMPARE(setup.base->space->input->isSelectingWindow(), false);
        setup.base->space->input->start_interactive_position_selection(callback);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), true);
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
        setup.base->space->input->start_interactive_position_selection(
            [&point2](const QPoint& p) { point2 = p; });
        QCOMPARE(point2, QPoint(-1, -1));

        // simulate left button press
        quint32 timestamp = 0;
        pointer_button_pressed(BTN_LEFT, timestamp++);
        // should not have ended the mode
        QCOMPARE(setup.base->space->input->isSelectingWindow(), true);
        QCOMPARE(point, QPoint());
        QVERIFY(!setup.base->space->input->pointer->focus.window);

        // updating the pointer should not change anything
        input::wayland::device_redirect_update(setup.base->space->input->pointer.get());
        QVERIFY(!setup.base->space->input->pointer->focus.window);
        // updating keyboard should also not change
        setup.base->space->input->keyboard->update();

        // perform a right button click
        pointer_button_pressed(BTN_RIGHT, timestamp++);
        pointer_button_released(BTN_RIGHT, timestamp++);
        // should not have ended the mode
        QCOMPARE(setup.base->space->input->isSelectingWindow(), true);
        QCOMPARE(point, QPoint());
        // now release
        pointer_button_released(BTN_LEFT, timestamp++);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), false);
        QCOMPARE(point, setup.base->space->input->globalPointer().toPoint());
        QCOMPARE(get_wayland_window(setup.base->space->input->pointer->focus.window), client);
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

    SECTION("select point touch")
    {
        // this test verifies point selection through touch works
        QPoint point;
        auto callback = [&point](const QPoint& p) { point = p; };

        // start the interaction
        QCOMPARE(setup.base->space->input->isSelectingWindow(), false);
        setup.base->space->input->start_interactive_position_selection(callback);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), true);
        QCOMPARE(point, QPoint());

        // let's create multiple touch points
        quint32 timestamp = 0;
        touch_down(0, QPointF(0, 1), timestamp++);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), true);
        touch_down(1, QPointF(10, 20), timestamp++);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), true);
        touch_down(2, QPointF(30, 40), timestamp++);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), true);

        // let's move our points
        touch_motion(0, QPointF(5, 10), timestamp++);
        touch_motion(2, QPointF(20, 25), timestamp++);
        touch_motion(1, QPointF(25, 35), timestamp++);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), true);
        touch_up(0, timestamp++);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), true);
        touch_up(2, timestamp++);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), true);
        touch_up(1, timestamp++);
        QCOMPARE(setup.base->space->input->isSelectingWindow(), false);
        QCOMPARE(point, QPoint(25, 35));
    }
}

}
