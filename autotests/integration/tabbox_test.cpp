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
#include "input/cursor.h"
#include "input/xkb/helpers.h"
#include "screens.h"
#include "win/control.h"
#include "win/space.h"
#include "win/stacking_order.h"
#include "win/tabbox/tabbox.h"
#include "win/wayland/window.h"

#include <KConfigGroup>
#include <Wrapland/Client/surface.h>

#include <linux/input.h>

using namespace Wrapland::Client;

namespace KWin
{

class TabBoxTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testMoveForward();
    void testMoveBackward();
    void testCapsLock();
};

void TabBoxTest::initTestCase()
{
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    KSharedConfigPtr c = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    c->group("TabBox").writeEntry("ShowTabBox", false);
    c->sync();
    kwinApp()->setConfig(c);
    qputenv("KWIN_XKB_DEFAULT_KEYMAP", "1");

    Test::app()->start();
    QVERIFY(startup_spy.size() || startup_spy.wait());
}

void TabBoxTest::init()
{
    Test::setup_wayland_connection();
    Test::app()->base.screens.setCurrent(0);
    input::get_cursor()->set_pos(QPoint(640, 512));
}

void TabBoxTest::cleanup()
{
    Test::destroy_wayland_connection();
}

void TabBoxTest::testCapsLock()
{
    // this test verifies that Alt+tab works correctly also when Capslock is on
    // bug 368590

    // first create three windows
    std::unique_ptr<Surface> surface1(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
    auto c1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
    QVERIFY(c1);
    QVERIFY(c1->control->active());
    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto c2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::red);
    QVERIFY(c2);
    QVERIFY(c2->control->active());
    std::unique_ptr<Surface> surface3(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
    auto c3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::red);
    QVERIFY(c3);
    QVERIFY(c3->control->active());

    QTRY_COMPARE(workspace()->stacking_order->sorted(), (std::deque<Toplevel*>{c1, c2, c3}));

    // Setup tabbox signal spies
    QSignalSpy tabboxAddedSpy(win::tabbox::self(), &win::tabbox::tabbox_added);
    QVERIFY(tabboxAddedSpy.isValid());
    QSignalSpy tabboxClosedSpy(win::tabbox::self(), &win::tabbox::tabbox_closed);
    QVERIFY(tabboxClosedSpy.isValid());

    // enable capslock
    quint32 timestamp = 0;
    Test::keyboard_key_pressed(KEY_CAPSLOCK, timestamp++);
    Test::keyboard_key_released(KEY_CAPSLOCK, timestamp++);
    QCOMPARE(input::xkb::get_active_keyboard_modifiers(kwinApp()->input), Qt::ShiftModifier);

    // press alt+tab
    Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
    QCOMPARE(input::xkb::get_active_keyboard_modifiers(kwinApp()->input),
             Qt::ShiftModifier | Qt::AltModifier);
    Test::keyboard_key_pressed(KEY_TAB, timestamp++);
    Test::keyboard_key_released(KEY_TAB, timestamp++);

    QVERIFY(tabboxAddedSpy.wait());
    QVERIFY(win::tabbox::self()->is_grabbed());

    // release alt
    Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
    QCOMPARE(tabboxClosedSpy.count(), 1);
    QCOMPARE(win::tabbox::self()->is_grabbed(), false);

    // release caps lock
    Test::keyboard_key_pressed(KEY_CAPSLOCK, timestamp++);
    Test::keyboard_key_released(KEY_CAPSLOCK, timestamp++);
    QCOMPARE(input::xkb::get_active_keyboard_modifiers(kwinApp()->input), Qt::NoModifier);
    QCOMPARE(tabboxClosedSpy.count(), 1);
    QCOMPARE(win::tabbox::self()->is_grabbed(), false);

    // Has walked backwards to the previously lowest client in the stacking order.
    QCOMPARE(workspace()->activeClient(), c1);
    QCOMPARE(workspace()->stacking_order->sorted(), (std::deque<Toplevel*>{c2, c3, c1}));

    surface3.reset();
    QVERIFY(Test::wait_for_destroyed(c3));
    surface2.reset();
    QVERIFY(Test::wait_for_destroyed(c2));
    surface1.reset();
    QVERIFY(Test::wait_for_destroyed(c1));
}

void TabBoxTest::testMoveForward()
{
    // this test verifies that Alt+tab works correctly moving forward

    // first create three windows
    std::unique_ptr<Surface> surface1(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
    auto c1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
    QVERIFY(c1);
    QVERIFY(c1->control->active());
    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto c2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::red);
    QVERIFY(c2);
    QVERIFY(c2->control->active());
    std::unique_ptr<Surface> surface3(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
    auto c3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::red);
    QVERIFY(c3);
    QVERIFY(c3->control->active());

    // Setup tabbox signal spies
    QSignalSpy tabboxAddedSpy(win::tabbox::self(), &win::tabbox::tabbox_added);
    QVERIFY(tabboxAddedSpy.isValid());
    QSignalSpy tabboxClosedSpy(win::tabbox::self(), &win::tabbox::tabbox_closed);
    QVERIFY(tabboxClosedSpy.isValid());

    // press alt+tab
    quint32 timestamp = 0;
    Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
    QCOMPARE(input::xkb::get_active_keyboard_modifiers(kwinApp()->input), Qt::AltModifier);
    Test::keyboard_key_pressed(KEY_TAB, timestamp++);
    Test::keyboard_key_released(KEY_TAB, timestamp++);

    QVERIFY(tabboxAddedSpy.wait());
    QVERIFY(win::tabbox::self()->is_grabbed());

    // release alt
    Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
    QCOMPARE(tabboxClosedSpy.count(), 1);
    QCOMPARE(win::tabbox::self()->is_grabbed(), false);
    QCOMPARE(workspace()->activeClient(), c2);

    surface3.reset();
    QVERIFY(Test::wait_for_destroyed(c3));
    surface2.reset();
    QVERIFY(Test::wait_for_destroyed(c2));
    surface1.reset();
    QVERIFY(Test::wait_for_destroyed(c1));
}

void TabBoxTest::testMoveBackward()
{
    // this test verifies that Alt+Shift+tab works correctly moving backward

    // first create three windows
    std::unique_ptr<Surface> surface1(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
    auto c1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
    QVERIFY(c1);
    QVERIFY(c1->control->active());
    std::unique_ptr<Surface> surface2(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
    auto c2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::red);
    QVERIFY(c2);
    QVERIFY(c2->control->active());
    std::unique_ptr<Surface> surface3(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface3(Test::create_xdg_shell_toplevel(surface3));
    auto c3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::red);
    QVERIFY(c3);
    QVERIFY(c3->control->active());

    // Setup tabbox signal spies
    QSignalSpy tabboxAddedSpy(win::tabbox::self(), &win::tabbox::tabbox_added);
    QVERIFY(tabboxAddedSpy.isValid());
    QSignalSpy tabboxClosedSpy(win::tabbox::self(), &win::tabbox::tabbox_closed);
    QVERIFY(tabboxClosedSpy.isValid());

    // press alt+shift+tab
    quint32 timestamp = 0;
    Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
    QCOMPARE(input::xkb::get_active_keyboard_modifiers(kwinApp()->input), Qt::AltModifier);
    Test::keyboard_key_pressed(KEY_LEFTSHIFT, timestamp++);
    QCOMPARE(input::xkb::get_active_keyboard_modifiers(kwinApp()->input),
             Qt::AltModifier | Qt::ShiftModifier);
    Test::keyboard_key_pressed(KEY_TAB, timestamp++);
    Test::keyboard_key_released(KEY_TAB, timestamp++);

    QVERIFY(tabboxAddedSpy.wait());
    QVERIFY(win::tabbox::self()->is_grabbed());

    // release alt
    Test::keyboard_key_released(KEY_LEFTSHIFT, timestamp++);
    QCOMPARE(tabboxClosedSpy.count(), 0);
    Test::keyboard_key_released(KEY_LEFTALT, timestamp++);
    QCOMPARE(tabboxClosedSpy.count(), 1);
    QCOMPARE(win::tabbox::self()->is_grabbed(), false);
    QCOMPARE(workspace()->activeClient(), c1);

    surface3.reset();
    QVERIFY(Test::wait_for_destroyed(c3));
    surface2.reset();
    QVERIFY(Test::wait_for_destroyed(c2));
    surface1.reset();
    QVERIFY(Test::wait_for_destroyed(c1));
}

}

WAYLANDTEST_MAIN(KWin::TabBoxTest)
#include "tabbox_test.moc"
