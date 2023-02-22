/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "input/xkb/helpers.h"
#include "win/control.h"
#include "win/space.h"
#include "win/stacking_order.h"
#include "win/tabbox/tabbox.h"
#include "win/wayland/window.h"

#include <KConfigGroup>
#include <Wrapland/Client/surface.h>

#include <linux/input.h>

using namespace Wrapland::Client;

namespace KWin::detail::test
{

TEST_CASE("tabbox", "[win]")
{
    qputenv("KWIN_XKB_DEFAULT_KEYMAP", "1");

    test::setup setup("tabbox");

    auto c = setup.base->config.main;
    c->group("TabBox").writeEntry("ShowTabBox", false);
    c->sync();

    setup.start();
    Test::setup_wayland_connection();
    Test::cursor()->set_pos(QPoint(640, 512));

    SECTION("move forward")
    {
        // this test verifies that Alt+tab works correctly moving forward

        // first create three windows
        std::unique_ptr<Surface> surface1(Test::create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
        QVERIFY(surface1);
        QVERIFY(shellSurface1);

        auto c1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
        QVERIFY(c1);
        QVERIFY(c1->control->active);
        std::unique_ptr<Surface> surface2(Test::create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
        QVERIFY(surface2);
        QVERIFY(shellSurface2);

        auto c2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::red);
        QVERIFY(c2);
        QVERIFY(c2->control->active);
        std::unique_ptr<Surface> surface3(Test::create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
        QVERIFY(surface3);
        QVERIFY(shellSurface3);

        auto c3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::red);
        QVERIFY(c3);
        QVERIFY(c3->control->active);

        // Setup tabbox signal spies
        QSignalSpy tabboxAddedSpy(setup.base->space->tabbox->qobject.get(),
                                  &win::tabbox_qobject::tabbox_added);
        QVERIFY(tabboxAddedSpy.isValid());
        QSignalSpy tabboxClosedSpy(setup.base->space->tabbox->qobject.get(),
                                   &win::tabbox_qobject::tabbox_closed);
        QVERIFY(tabboxClosedSpy.isValid());

        // press alt+tab
        quint32 timestamp = 0;
        Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
        QCOMPARE(input::xkb::get_active_keyboard_modifiers(*setup.base->input), Qt::AltModifier);
        Test::keyboard_key_pressed(KEY_TAB, timestamp++);
        Test::keyboard_key_released(KEY_TAB, timestamp++);

        QVERIFY(tabboxAddedSpy.wait());
        QVERIFY(setup.base->space->tabbox->is_grabbed());

        // release alt
        Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
        QCOMPARE(tabboxClosedSpy.count(), 1);
        QCOMPARE(setup.base->space->tabbox->is_grabbed(), false);
        QCOMPARE(Test::get_wayland_window(setup.base->space->stacking.active), c2);

        surface3.reset();
        QVERIFY(Test::wait_for_destroyed(c3));
        surface2.reset();
        QVERIFY(Test::wait_for_destroyed(c2));
        surface1.reset();
        QVERIFY(Test::wait_for_destroyed(c1));
    }

    SECTION("move backward")
    {
        // this test verifies that Alt+Shift+tab works correctly moving backward

        // first create three windows
        std::unique_ptr<Surface> surface1(Test::create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
        QVERIFY(surface1);
        QVERIFY(shellSurface1);

        auto c1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
        QVERIFY(c1);
        QVERIFY(c1->control->active);
        std::unique_ptr<Surface> surface2(Test::create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
        QVERIFY(surface2);
        QVERIFY(shellSurface2);

        auto c2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::red);
        QVERIFY(c2);
        QVERIFY(c2->control->active);
        std::unique_ptr<Surface> surface3(Test::create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
        QVERIFY(surface3);
        QVERIFY(shellSurface3);

        auto c3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::red);
        QVERIFY(c3);
        QVERIFY(c3->control->active);

        // Setup tabbox signal spies
        QSignalSpy tabboxAddedSpy(setup.base->space->tabbox->qobject.get(),
                                  &win::tabbox_qobject::tabbox_added);
        QVERIFY(tabboxAddedSpy.isValid());
        QSignalSpy tabboxClosedSpy(setup.base->space->tabbox->qobject.get(),
                                   &win::tabbox_qobject::tabbox_closed);
        QVERIFY(tabboxClosedSpy.isValid());

        // press alt+shift+tab
        quint32 timestamp = 0;
        Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
        QCOMPARE(input::xkb::get_active_keyboard_modifiers(*setup.base->input), Qt::AltModifier);
        Test::keyboard_key_pressed(KEY_LEFTSHIFT, timestamp++);
        REQUIRE(input::xkb::get_active_keyboard_modifiers(*setup.base->input)
                == (Qt::AltModifier | Qt::ShiftModifier));
        Test::keyboard_key_pressed(KEY_TAB, timestamp++);
        Test::keyboard_key_released(KEY_TAB, timestamp++);

        QVERIFY(tabboxAddedSpy.wait());
        QVERIFY(setup.base->space->tabbox->is_grabbed());

        // release alt
        Test::keyboard_key_released(KEY_LEFTSHIFT, timestamp++);
        QCOMPARE(tabboxClosedSpy.count(), 0);
        Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
        QCOMPARE(tabboxClosedSpy.count(), 1);
        QCOMPARE(setup.base->space->tabbox->is_grabbed(), false);
        QCOMPARE(Test::get_wayland_window(setup.base->space->stacking.active), c1);

        surface3.reset();
        QVERIFY(Test::wait_for_destroyed(c3));
        surface2.reset();
        QVERIFY(Test::wait_for_destroyed(c2));
        surface1.reset();
        QVERIFY(Test::wait_for_destroyed(c1));
    }

    SECTION("caps lock")
    {
        // this test verifies that Alt+tab works correctly also when Capslock is on
        // bug 368590

        // first create three windows
        std::unique_ptr<Surface> surface1(Test::create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
        QVERIFY(surface1);
        QVERIFY(shellSurface1);

        auto c1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
        QVERIFY(c1);
        QVERIFY(c1->control->active);
        std::unique_ptr<Surface> surface2(Test::create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
        QVERIFY(surface2);
        QVERIFY(shellSurface2);

        auto c2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::red);
        QVERIFY(c2);
        QVERIFY(c2->control->active);
        std::unique_ptr<Surface> surface3(Test::create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
        QVERIFY(surface3);
        QVERIFY(shellSurface3);

        auto c3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::red);
        QVERIFY(c3);
        QVERIFY(c3->control->active);

        QTRY_COMPARE(setup.base->space->stacking.order.stack,
                     (std::deque<Test::space::window_t>{c1, c2, c3}));

        // Setup tabbox signal spies
        QSignalSpy tabboxAddedSpy(setup.base->space->tabbox->qobject.get(),
                                  &win::tabbox_qobject::tabbox_added);
        QVERIFY(tabboxAddedSpy.isValid());
        QSignalSpy tabboxClosedSpy(setup.base->space->tabbox->qobject.get(),
                                   &win::tabbox_qobject::tabbox_closed);
        QVERIFY(tabboxClosedSpy.isValid());

        // enable capslock
        quint32 timestamp = 0;
        Test::keyboard_key_pressed(KEY_CAPSLOCK, timestamp++);
        Test::keyboard_key_released(KEY_CAPSLOCK, timestamp++);
        QCOMPARE(input::xkb::get_active_keyboard_modifiers(*setup.base->input), Qt::ShiftModifier);

        // press alt+tab
        Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
        REQUIRE(input::xkb::get_active_keyboard_modifiers(*setup.base->input)
                == (Qt::ShiftModifier | Qt::AltModifier));
        Test::keyboard_key_pressed(KEY_TAB, timestamp++);
        Test::keyboard_key_released(KEY_TAB, timestamp++);

        QVERIFY(tabboxAddedSpy.wait());
        QVERIFY(setup.base->space->tabbox->is_grabbed());

        // release alt
        Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
        QCOMPARE(tabboxClosedSpy.count(), 1);
        QCOMPARE(setup.base->space->tabbox->is_grabbed(), false);

        // release caps lock
        Test::keyboard_key_pressed(KEY_CAPSLOCK, timestamp++);
        Test::keyboard_key_released(KEY_CAPSLOCK, timestamp++);
        QCOMPARE(input::xkb::get_active_keyboard_modifiers(*setup.base->input), Qt::NoModifier);
        QCOMPARE(tabboxClosedSpy.count(), 1);
        QCOMPARE(setup.base->space->tabbox->is_grabbed(), false);

        // Has walked backwards to the previously lowest client in the stacking order.
        QCOMPARE(Test::get_wayland_window(setup.base->space->stacking.active), c1);
        QCOMPARE(setup.base->space->stacking.order.stack,
                 (std::deque<Test::space::window_t>{c2, c3, c1}));

        surface3.reset();
        QVERIFY(Test::wait_for_destroyed(c3));
        surface2.reset();
        QVERIFY(Test::wait_for_destroyed(c2));
        surface1.reset();
        QVERIFY(Test::wait_for_destroyed(c1));
    }
}

}
