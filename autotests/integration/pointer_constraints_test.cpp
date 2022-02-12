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
#include "input/keyboard_redirect.h"
#include "input/pointer_redirect.h"
#include "input/redirect.h"
#include "screens.h"
#include "win/move.h"
#include "win/space.h"

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

#include <linux/input.h>

#include <functional>

using namespace Wrapland::Client;

typedef std::function<QPoint(const QRect&)> PointerFunc;
Q_DECLARE_METATYPE(PointerFunc)

namespace KWin
{

class TestPointerConstraints : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testConfinedPointer_data();
    void testConfinedPointer();
    void testLockedPointer();
    void testCloseWindowWithLockedPointer();
};

void TestPointerConstraints::initTestCase()
{
    qRegisterMetaType<PointerFunc>();
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    // set custom config which disables the OnScreenNotification
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    KConfigGroup group = config->group("OnScreenNotification");
    group.writeEntry(QStringLiteral("QmlPath"), QString("/does/not/exist.qml"));
    group.sync();

    kwinApp()->setConfig(config);

    Test::app()->start();
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.size() || startup_spy.wait());
    QCOMPARE(Test::app()->base.screens.count(), 2);
    QCOMPARE(Test::app()->base.screens.geometry(0), QRect(0, 0, 1280, 1024));
    QCOMPARE(Test::app()->base.screens.geometry(1), QRect(1280, 0, 1280, 1024));
}

void TestPointerConstraints::init()
{
    Test::setup_wayland_connection(Test::global_selection::seat
                                   | Test::global_selection::pointer_constraints);
    QVERIFY(Test::wait_for_wayland_pointer());

    Test::app()->base.screens.setCurrent(0);
    input::get_cursor()->set_pos(QPoint(1280, 512));
}

void TestPointerConstraints::cleanup()
{
    Test::destroy_wayland_connection();
}

void TestPointerConstraints::testConfinedPointer_data()
{
    QTest::addColumn<PointerFunc>("positionFunction");
    QTest::addColumn<int>("xOffset");
    QTest::addColumn<int>("yOffset");
    PointerFunc bottomLeft = &QRect::bottomLeft;
    PointerFunc bottomRight = &QRect::bottomRight;
    PointerFunc topRight = &QRect::topRight;
    PointerFunc topLeft = &QRect::topLeft;

    QTest::newRow("bottomLeft") << bottomLeft << -1 << 1;
    QTest::newRow("bottomRight") << bottomRight << 1 << 1;
    QTest::newRow("topLeft") << topLeft << -1 << -1;
    QTest::newRow("topRight") << topRight << 1 << -1;
}

void TestPointerConstraints::testConfinedPointer()
{
    // this test sets up a Surface with a confined pointer
    // simple interaction test to verify that the pointer gets confined
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
    if (c->pos() == QPoint(0, 0)) {
        win::move(c, QPoint(1, 1));
    }
    QVERIFY(!c->frameGeometry().contains(input::get_cursor()->pos()));

    // now let's confine
    QCOMPARE(kwinApp()->input->redirect->pointer()->isConstrained(), false);
    input::get_cursor()->set_pos(c->frameGeometry().center());
    QCOMPARE(kwinApp()->input->redirect->pointer()->isConstrained(), true);
    QVERIFY(confinedSpy.wait());

    // picking a position outside the window geometry should not move pointer
    QSignalSpy pointerPositionChangedSpy(kwinApp()->input->redirect.get(),
                                         &input::redirect::globalPointerChanged);
    QVERIFY(pointerPositionChangedSpy.isValid());
    input::get_cursor()->set_pos(QPoint(1280, 512));
    QVERIFY(pointerPositionChangedSpy.isEmpty());
    QCOMPARE(input::get_cursor()->pos(), c->frameGeometry().center());

    // TODO: test relative motion
    QFETCH(PointerFunc, positionFunction);
    const QPoint position = positionFunction(c->frameGeometry());
    input::get_cursor()->set_pos(position);
    QCOMPARE(pointerPositionChangedSpy.count(), 1);
    QCOMPARE(input::get_cursor()->pos(), position);
    // moving one to right should not be possible
    QFETCH(int, xOffset);
    input::get_cursor()->set_pos(position + QPoint(xOffset, 0));
    QCOMPARE(pointerPositionChangedSpy.count(), 1);
    QCOMPARE(input::get_cursor()->pos(), position);
    // moving one to bottom should not be possible
    QFETCH(int, yOffset);
    input::get_cursor()->set_pos(position + QPoint(0, yOffset));
    QCOMPARE(pointerPositionChangedSpy.count(), 1);
    QCOMPARE(input::get_cursor()->pos(), position);

    // modifier + click should be ignored
    // first ensure the settings are ok
    KConfigGroup group = kwinApp()->config()->group("MouseBindings");
    group.writeEntry("CommandAllKey", QStringLiteral("Meta"));
    group.writeEntry("CommandAll1", "Move");
    group.writeEntry("CommandAll2", "Move");
    group.writeEntry("CommandAll3", "Move");
    group.writeEntry("CommandAllWheel", "change opacity");
    group.sync();
    workspace()->slotReconfigure();
    QCOMPARE(kwinApp()->options->commandAllModifier(), Qt::MetaModifier);
    QCOMPARE(kwinApp()->options->commandAll1(), base::options::MouseUnrestrictedMove);
    QCOMPARE(kwinApp()->options->commandAll2(), base::options::MouseUnrestrictedMove);
    QCOMPARE(kwinApp()->options->commandAll3(), base::options::MouseUnrestrictedMove);

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
    workspace()->activateClient(nullptr);
    QVERIFY(unconfinedSpy.wait());
    QCOMPARE(kwinApp()->input->redirect->pointer()->isConstrained(), false);

    // reconfine pointer (this time with persistent life time)
    confinedPointer.reset(Test::get_client().interfaces.pointer_constraints->confinePointer(
        surface.get(), pointer.get(), nullptr, PointerConstraints::LifeTime::Persistent));
    QSignalSpy confinedSpy2(confinedPointer.get(), &ConfinedPointer::confined);
    QVERIFY(confinedSpy2.isValid());
    QSignalSpy unconfinedSpy2(confinedPointer.get(), &ConfinedPointer::unconfined);
    QVERIFY(unconfinedSpy2.isValid());

    // activate it again, this confines again
    workspace()->activateClient(kwinApp()->input->redirect->pointer()->focus());
    QVERIFY(confinedSpy2.wait());
    QCOMPARE(kwinApp()->input->redirect->pointer()->isConstrained(), true);

    // deactivate the client one more time with the persistent life time constraint, this should
    // unconfine
    workspace()->activateClient(nullptr);
    QVERIFY(unconfinedSpy2.wait());
    QCOMPARE(kwinApp()->input->redirect->pointer()->isConstrained(), false);
    // activate it again, this confines again
    workspace()->activateClient(kwinApp()->input->redirect->pointer()->focus());
    QVERIFY(confinedSpy2.wait());
    QCOMPARE(kwinApp()->input->redirect->pointer()->isConstrained(), true);

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
    QCOMPARE(kwinApp()->input->redirect->pointer()->isConstrained(), false);
    // and set a full region again, that should confine
    confinedPointer->setRegion(nullptr);
    surface->commit(Surface::CommitFlag::None);
    QVERIFY(confinedSpy2.wait());
    QCOMPARE(kwinApp()->input->redirect->pointer()->isConstrained(), true);

    // delete pointer confine
    confinedPointer.reset(nullptr);
    Test::flush_wayland_connection();

    QSignalSpy constraintsChangedSpy(kwinApp()->input->redirect->pointer()->focus()->surface(),
                                     &Wrapland::Server::Surface::pointerConstraintsChanged);
    QVERIFY(constraintsChangedSpy.isValid());
    QVERIFY(constraintsChangedSpy.wait());

    // should be unconfined
    QCOMPARE(kwinApp()->input->redirect->pointer()->isConstrained(), false);

    // confine again
    confinedPointer.reset(Test::get_client().interfaces.pointer_constraints->confinePointer(
        surface.get(), pointer.get(), nullptr, PointerConstraints::LifeTime::Persistent));
    QSignalSpy confinedSpy3(confinedPointer.get(), &ConfinedPointer::confined);
    QVERIFY(confinedSpy3.isValid());
    QVERIFY(confinedSpy3.wait());
    QCOMPARE(kwinApp()->input->redirect->pointer()->isConstrained(), true);

    // and now unmap
    shellSurface.reset();
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(c));
    QCOMPARE(kwinApp()->input->redirect->pointer()->isConstrained(), false);
}

void TestPointerConstraints::testLockedPointer()
{
    // this test sets up a Surface with a locked pointer
    // simple interaction test to verify that the pointer gets locked
    // the various ways to unlock are not tested as that's already verified by testConfinedPointer
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
    QVERIFY(!c->frameGeometry().contains(input::get_cursor()->pos()));

    // now let's lock
    QCOMPARE(kwinApp()->input->redirect->pointer()->isConstrained(), false);
    input::get_cursor()->set_pos(c->frameGeometry().center());
    QCOMPARE(input::get_cursor()->pos(), c->frameGeometry().center());
    QCOMPARE(kwinApp()->input->redirect->pointer()->isConstrained(), true);
    QVERIFY(lockedSpy.wait());

    // try to move the pointer
    // TODO: add relative pointer
    input::get_cursor()->set_pos(c->frameGeometry().center() + QPoint(1, 1));
    QCOMPARE(input::get_cursor()->pos(), c->frameGeometry().center());

    // deactivate the client, this should unlock
    workspace()->activateClient(nullptr);
    QCOMPARE(kwinApp()->input->redirect->pointer()->isConstrained(), false);
    QVERIFY(unlockedSpy.wait());

    // moving cursor should be allowed again
    input::get_cursor()->set_pos(c->frameGeometry().center() + QPoint(1, 1));
    QCOMPARE(input::get_cursor()->pos(), c->frameGeometry().center() + QPoint(1, 1));

    lockedPointer.reset(Test::get_client().interfaces.pointer_constraints->lockPointer(
        surface.get(), pointer.get(), nullptr, PointerConstraints::LifeTime::Persistent));
    QSignalSpy lockedSpy2(lockedPointer.get(), &LockedPointer::locked);
    QVERIFY(lockedSpy2.isValid());

    // activate the client again, this should lock again
    workspace()->activateClient(kwinApp()->input->redirect->pointer()->focus());
    QVERIFY(lockedSpy2.wait());
    QCOMPARE(kwinApp()->input->redirect->pointer()->isConstrained(), true);

    // try to move the pointer
    QCOMPARE(kwinApp()->input->redirect->pointer()->isConstrained(), true);
    input::get_cursor()->set_pos(c->frameGeometry().center());
    QCOMPARE(input::get_cursor()->pos(), c->frameGeometry().center() + QPoint(1, 1));

    // delete pointer lock
    lockedPointer.reset(nullptr);
    Test::flush_wayland_connection();

    QSignalSpy constraintsChangedSpy(kwinApp()->input->redirect->pointer()->focus()->surface(),
                                     &Wrapland::Server::Surface::pointerConstraintsChanged);
    QVERIFY(constraintsChangedSpy.isValid());
    QVERIFY(constraintsChangedSpy.wait());

    // moving cursor should be allowed again
    QCOMPARE(kwinApp()->input->redirect->pointer()->isConstrained(), false);
    input::get_cursor()->set_pos(c->frameGeometry().center());
    QCOMPARE(input::get_cursor()->pos(), c->frameGeometry().center());
}

void TestPointerConstraints::testCloseWindowWithLockedPointer()
{
    // test case which verifies that the pointer gets unlocked when the window for it gets closed
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
    QVERIFY(!c->frameGeometry().contains(input::get_cursor()->pos()));

    // now let's lock
    QCOMPARE(kwinApp()->input->redirect->pointer()->isConstrained(), false);
    input::get_cursor()->set_pos(c->frameGeometry().center());
    QCOMPARE(input::get_cursor()->pos(), c->frameGeometry().center());
    QCOMPARE(kwinApp()->input->redirect->pointer()->isConstrained(), true);
    QVERIFY(lockedSpy.wait());

    // close the window
    shellSurface.reset();
    surface.reset();
    // this should result in unlocked
    QVERIFY(unlockedSpy.wait());
    QCOMPARE(kwinApp()->input->redirect->pointer()->isConstrained(), false);
}

}

WAYLANDTEST_MAIN(KWin::TestPointerConstraints)
#include "pointer_constraints_test.moc"
