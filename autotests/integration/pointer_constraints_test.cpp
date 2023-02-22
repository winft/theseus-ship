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
#include "win/move.h"
#include "win/space.h"
#include "win/space_reconfigure.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/keyboard.h>
#include <Wrapland/Client/pointer.h>
#include <Wrapland/Client/pointerconstraints.h>
#include <Wrapland/Client/region.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>

#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/surface.h>

#include <catch2/generators/catch_generators.hpp>
#include <functional>
#include <linux/input.h>

using namespace Wrapland::Client;

namespace KWin::detail::test
{

TEST_CASE("pointer constraints", "[input]")
{
    test::setup setup("pointer-constraints");

    // set custom config which disables the OnScreenNotification
    auto group = setup.base->config.main->group("OnScreenNotification");
    group.writeEntry(QStringLiteral("QmlPath"), QString("/does/not/exist.qml"));
    group.sync();

    setup.start();
    setup.set_outputs(2);
    Test::test_outputs_default();
    Test::setup_wayland_connection(Test::global_selection::seat
                                   | Test::global_selection::pointer_constraints);

    SECTION("confined pointer")
    {
        // this test sets up a Surface with a confined pointer
        // simple interaction test to verify that the pointer gets confined

        struct data {
            std::function<QPoint(const QRect&)> position_function;
            QPoint offset;
        };

        auto test_data = GENERATE(data{&QRect::bottomLeft, {-1, 1}},
                                  data{&QRect::bottomRight, {1, 1}},
                                  data{&QRect::topLeft, {-1, -1}},
                                  data{&QRect::topRight, {1, -1}});

        std::unique_ptr<Surface> surface(Test::create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
        std::unique_ptr<Pointer> pointer(Test::get_client().interfaces.seat->createPointer());
        std::unique_ptr<ConfinedPointer> confinedPointer(
            Test::get_client().interfaces.pointer_constraints->confinePointer(
                surface.get(), pointer.get(), nullptr, PointerConstraints::LifeTime::OneShot));
        QSignalSpy confinedSpy(confinedPointer.get(), &ConfinedPointer::confined);
        QVERIFY(confinedSpy.isValid());
        QSignalSpy unconfinedSpy(confinedPointer.get(), &ConfinedPointer::unconfined);
        QVERIFY(unconfinedSpy.isValid());

        // now map the window
        auto c = Test::render_and_wait_for_shown(surface, QSize(100, 100), Qt::blue);
        QVERIFY(c);
        if (c->geo.pos() == QPoint(0, 0)) {
            win::move(c, QPoint(1, 1));
        }
        QVERIFY(!c->geo.frame.contains(Test::cursor()->pos()));

        // now let's confine
        QCOMPARE(setup.base->space->input->pointer->isConstrained(), false);
        Test::cursor()->set_pos(c->geo.frame.center());
        QCOMPARE(setup.base->space->input->pointer->isConstrained(), true);
        QVERIFY(confinedSpy.wait());

        // picking a position outside the window geometry should not move pointer
        QSignalSpy pointerPositionChangedSpy(setup.base->space->input->qobject.get(),
                                             &input::redirect_qobject::globalPointerChanged);
        QVERIFY(pointerPositionChangedSpy.isValid());
        Test::cursor()->set_pos(QPoint(1280, 512));
        QVERIFY(pointerPositionChangedSpy.isEmpty());
        QCOMPARE(Test::cursor()->pos(), c->geo.frame.center());

        // TODO: test relative motion
        const QPoint position = test_data.position_function(c->geo.frame);
        Test::cursor()->set_pos(position);
        QCOMPARE(pointerPositionChangedSpy.count(), 1);
        QCOMPARE(Test::cursor()->pos(), position);

        // moving one to right should not be possible
        Test::cursor()->set_pos(position + QPoint(test_data.offset.x(), 0));
        QCOMPARE(pointerPositionChangedSpy.count(), 1);
        QCOMPARE(Test::cursor()->pos(), position);

        // moving one to bottom should not be possible
        Test::cursor()->set_pos(position + QPoint(0, test_data.offset.y()));
        QCOMPARE(pointerPositionChangedSpy.count(), 1);
        QCOMPARE(Test::cursor()->pos(), position);

        // modifier + click should be ignored
        // first ensure the settings are ok
        auto group = setup.base->config.main->group("MouseBindings");
        group.writeEntry("CommandAllKey", QStringLiteral("Meta"));
        group.writeEntry("CommandAll1", "Move");
        group.writeEntry("CommandAll2", "Move");
        group.writeEntry("CommandAll3", "Move");
        group.writeEntry("CommandAllWheel", "change opacity");
        group.sync();
        win::space_reconfigure(*setup.base->space);
        QCOMPARE(setup.base->options->qobject->commandAllModifier(), Qt::MetaModifier);
        QCOMPARE(setup.base->options->qobject->commandAll1(),
                 base::options_qobject::MouseUnrestrictedMove);
        QCOMPARE(setup.base->options->qobject->commandAll2(),
                 base::options_qobject::MouseUnrestrictedMove);
        QCOMPARE(setup.base->options->qobject->commandAll3(),
                 base::options_qobject::MouseUnrestrictedMove);

        quint32 timestamp = 1;
        Test::keyboard_key_pressed(KEY_LEFTALT, timestamp++);
        Test::pointer_button_pressed(BTN_LEFT, timestamp++);
        QVERIFY(!win::is_move(c));
        Test::pointer_button_released(BTN_LEFT, timestamp++);

        // set the opacity to 0.5
        c->setOpacity(0.5);
        QCOMPARE(c->opacity(), 0.5);

        // pointer is confined so shortcut should not work
        Test::pointer_axis_vertical(-5, timestamp++, 0);
        QCOMPARE(c->opacity(), 0.5);
        Test::pointer_axis_vertical(5, timestamp++, 0);
        QCOMPARE(c->opacity(), 0.5);

        Test::keyboard_key_released(KEY_LEFTALT, timestamp++);

        // deactivate the client, this should unconfine
        win::deactivate_window(*setup.base->space);
        QVERIFY(unconfinedSpy.wait());
        QCOMPARE(setup.base->space->input->pointer->isConstrained(), false);

        // reconfine pointer (this time with persistent life time)
        confinedPointer.reset(Test::get_client().interfaces.pointer_constraints->confinePointer(
            surface.get(), pointer.get(), nullptr, PointerConstraints::LifeTime::Persistent));
        QSignalSpy confinedSpy2(confinedPointer.get(), &ConfinedPointer::confined);
        QVERIFY(confinedSpy2.isValid());
        QSignalSpy unconfinedSpy2(confinedPointer.get(), &ConfinedPointer::unconfined);
        QVERIFY(unconfinedSpy2.isValid());

        // activate it again, this confines again
        auto pointer_focus_window
            = Test::get_wayland_window(setup.base->space->input->pointer->focus.window);
        QVERIFY(pointer_focus_window);
        win::activate_window(*setup.base->space, *pointer_focus_window);
        QVERIFY(confinedSpy2.wait());
        QCOMPARE(setup.base->space->input->pointer->isConstrained(), true);

        // deactivate the client one more time with the persistent life time constraint, this should
        // unconfine
        win::deactivate_window(*setup.base->space);
        QVERIFY(unconfinedSpy2.wait());
        QCOMPARE(setup.base->space->input->pointer->isConstrained(), false);

        // activate it again, this confines again
        pointer_focus_window
            = Test::get_wayland_window(setup.base->space->input->pointer->focus.window);
        QVERIFY(pointer_focus_window);
        win::activate_window(*setup.base->space, *pointer_focus_window);
        QVERIFY(confinedSpy2.wait());
        QCOMPARE(setup.base->space->input->pointer->isConstrained(), true);

        // create a second window and move it above our constrained window
        std::unique_ptr<Surface> surface2(Test::create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
        auto c2 = Test::render_and_wait_for_shown(surface2, QSize(1280, 1024), Qt::blue);
        QVERIFY(c2);
        QVERIFY(unconfinedSpy2.wait());
        // and unmapping the second window should confine again
        shellSurface2.reset();
        surface2.reset();
        QVERIFY(confinedSpy2.wait());

        // let's set a region which results in unconfined
        auto r = Test::get_client().interfaces.compositor->createRegion(QRegion(2, 2, 3, 3));
        confinedPointer->setRegion(r.get());
        surface->commit(Surface::CommitFlag::None);
        QVERIFY(unconfinedSpy2.wait());
        QCOMPARE(setup.base->space->input->pointer->isConstrained(), false);
        // and set a full region again, that should confine
        confinedPointer->setRegion(nullptr);
        surface->commit(Surface::CommitFlag::None);
        QVERIFY(confinedSpy2.wait());
        QCOMPARE(setup.base->space->input->pointer->isConstrained(), true);

        // delete pointer confine
        confinedPointer.reset(nullptr);
        Test::flush_wayland_connection();

        pointer_focus_window
            = Test::get_wayland_window(setup.base->space->input->pointer->focus.window);
        QVERIFY(pointer_focus_window);
        QSignalSpy constraintsChangedSpy(pointer_focus_window->surface,
                                         &Wrapland::Server::Surface::pointerConstraintsChanged);
        QVERIFY(constraintsChangedSpy.isValid());
        QVERIFY(constraintsChangedSpy.wait());

        // should be unconfined
        QCOMPARE(setup.base->space->input->pointer->isConstrained(), false);

        // confine again
        confinedPointer.reset(Test::get_client().interfaces.pointer_constraints->confinePointer(
            surface.get(), pointer.get(), nullptr, PointerConstraints::LifeTime::Persistent));
        QSignalSpy confinedSpy3(confinedPointer.get(), &ConfinedPointer::confined);
        QVERIFY(confinedSpy3.isValid());
        QVERIFY(confinedSpy3.wait());
        QCOMPARE(setup.base->space->input->pointer->isConstrained(), true);

        // and now unmap
        shellSurface.reset();
        surface.reset();
        QVERIFY(Test::wait_for_destroyed(c));
        QCOMPARE(setup.base->space->input->pointer->isConstrained(), false);
    }

    SECTION("locked pointer")
    {
        // this test sets up a Surface with a locked pointer
        // simple interaction test to verify that the pointer gets locked
        // the various ways to unlock are not tested as that's already verified by
        // testConfinedPointer
        std::unique_ptr<Surface> surface(Test::create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
        QVERIFY(surface);
        QVERIFY(shellSurface);

        std::unique_ptr<Pointer> pointer(Test::get_client().interfaces.seat->createPointer());
        std::unique_ptr<LockedPointer> lockedPointer(
            Test::get_client().interfaces.pointer_constraints->lockPointer(
                surface.get(), pointer.get(), nullptr, PointerConstraints::LifeTime::OneShot));
        QSignalSpy lockedSpy(lockedPointer.get(), &LockedPointer::locked);
        QVERIFY(lockedSpy.isValid());
        QSignalSpy unlockedSpy(lockedPointer.get(), &LockedPointer::unlocked);
        QVERIFY(unlockedSpy.isValid());

        // now map the window
        auto c = Test::render_and_wait_for_shown(surface, QSize(100, 100), Qt::blue);
        QVERIFY(c);
        QVERIFY(!c->geo.frame.contains(Test::cursor()->pos()));

        // now let's lock
        QCOMPARE(setup.base->space->input->pointer->isConstrained(), false);
        Test::cursor()->set_pos(c->geo.frame.center());
        QCOMPARE(Test::cursor()->pos(), c->geo.frame.center());
        QCOMPARE(setup.base->space->input->pointer->isConstrained(), true);
        QVERIFY(lockedSpy.wait());

        // try to move the pointer
        // TODO: add relative pointer
        Test::cursor()->set_pos(c->geo.frame.center() + QPoint(1, 1));
        QCOMPARE(Test::cursor()->pos(), c->geo.frame.center());

        // deactivate the client, this should unlock
        win::deactivate_window(*setup.base->space);
        QCOMPARE(setup.base->space->input->pointer->isConstrained(), false);
        QVERIFY(unlockedSpy.wait());

        // moving cursor should be allowed again
        Test::cursor()->set_pos(c->geo.frame.center() + QPoint(1, 1));
        QCOMPARE(Test::cursor()->pos(), c->geo.frame.center() + QPoint(1, 1));

        lockedPointer.reset(Test::get_client().interfaces.pointer_constraints->lockPointer(
            surface.get(), pointer.get(), nullptr, PointerConstraints::LifeTime::Persistent));
        QSignalSpy lockedSpy2(lockedPointer.get(), &LockedPointer::locked);
        QVERIFY(lockedSpy2.isValid());

        // activate the client again, this should lock again
        auto pointer_focus_window
            = Test::get_wayland_window(setup.base->space->input->pointer->focus.window);
        QVERIFY(pointer_focus_window);
        win::activate_window(*setup.base->space, *pointer_focus_window);
        QVERIFY(lockedSpy2.wait());
        QCOMPARE(setup.base->space->input->pointer->isConstrained(), true);

        // try to move the pointer
        QCOMPARE(setup.base->space->input->pointer->isConstrained(), true);
        Test::cursor()->set_pos(c->geo.frame.center());
        QCOMPARE(Test::cursor()->pos(), c->geo.frame.center() + QPoint(1, 1));

        // delete pointer lock
        lockedPointer.reset(nullptr);
        Test::flush_wayland_connection();

        pointer_focus_window
            = Test::get_wayland_window(setup.base->space->input->pointer->focus.window);
        QVERIFY(pointer_focus_window);
        QSignalSpy constraintsChangedSpy(pointer_focus_window->surface,
                                         &Wrapland::Server::Surface::pointerConstraintsChanged);
        QVERIFY(constraintsChangedSpy.isValid());
        QVERIFY(constraintsChangedSpy.wait());

        // moving cursor should be allowed again
        QCOMPARE(setup.base->space->input->pointer->isConstrained(), false);
        Test::cursor()->set_pos(c->geo.frame.center());
        QCOMPARE(Test::cursor()->pos(), c->geo.frame.center());
    }

    SECTION("close window with locked pointer")
    {
        // test case which verifies that the pointer gets unlocked when the window for it gets
        // closed
        std::unique_ptr<Surface> surface(Test::create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
        std::unique_ptr<Pointer> pointer(Test::get_client().interfaces.seat->createPointer());
        std::unique_ptr<LockedPointer> lockedPointer(
            Test::get_client().interfaces.pointer_constraints->lockPointer(
                surface.get(), pointer.get(), nullptr, PointerConstraints::LifeTime::OneShot));
        QSignalSpy lockedSpy(lockedPointer.get(), &LockedPointer::locked);
        QVERIFY(lockedSpy.isValid());
        QSignalSpy unlockedSpy(lockedPointer.get(), &LockedPointer::unlocked);
        QVERIFY(unlockedSpy.isValid());

        // now map the window
        auto c = Test::render_and_wait_for_shown(surface, QSize(100, 100), Qt::blue);
        QVERIFY(c);
        QVERIFY(!c->geo.frame.contains(Test::cursor()->pos()));

        // now let's lock
        QCOMPARE(setup.base->space->input->pointer->isConstrained(), false);
        Test::cursor()->set_pos(c->geo.frame.center());
        QCOMPARE(Test::cursor()->pos(), c->geo.frame.center());
        QCOMPARE(setup.base->space->input->pointer->isConstrained(), true);
        QVERIFY(lockedSpy.wait());

        // close the window
        shellSurface.reset();
        surface.reset();
        // this should result in unlocked
        QVERIFY(unlockedSpy.wait());
        QCOMPARE(setup.base->space->input->pointer->isConstrained(), false);
    }
}

}
