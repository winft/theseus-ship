/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2017 Martin Flöser <mgraesslin@kde.org>

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

#include "win/actions.h"
#include "win/desktop_set.h"
#include "win/screen.h"
#include "win/space.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/idle_notify_v1.h>
#include <Wrapland/Client/idleinhibit.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>

#include <Wrapland/Server/display.h>
#include <Wrapland/Server/kde_idle.h>

using namespace Wrapland::Client;
using Wrapland::Server::KdeIdle;

namespace KWin
{

class TestIdleInhibition : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testInhibit();
    void testDontInhibitWhenNotOnCurrentDesktop();
    void testDontInhibitWhenMinimized();
    void testDontInhibitWhenUnmapped();
    void testDontInhibitWhenLeftCurrentDesktop();
};

void TestIdleInhibition::initTestCase()
{
    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    Test::app()->start();
    Test::app()->set_outputs(2);
    QVERIFY(startup_spy.wait());
}

void TestIdleInhibition::init()
{
    Test::setup_wayland_connection(Test::global_selection::idle_inhibition
                                   | Test::global_selection::seat);
}

void TestIdleInhibition::cleanup()
{
    Test::destroy_wayland_connection();

    auto& vd_manager = Test::app()->base.space->virtual_desktop_manager;
    vd_manager->setCount(1);
    QCOMPARE(vd_manager->count(), 1u);
}

void TestIdleInhibition::testInhibit()
{
    auto& idle = Test::app()->base.input->idle;
    QCOMPARE(idle.inhibit_count, 0);

    // now create window
    std::unique_ptr<Surface> surface(Test::create_surface());
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));

    auto notification = std::unique_ptr<Wrapland::Client::idle_notification_v1>(
        Test::get_client().interfaces.idle_notifier->get_notification(
            0, Test::get_client().interfaces.seat.get()));
    QVERIFY(notification->isValid());

    QSignalSpy idle_spy(notification.get(), &Wrapland::Client::idle_notification_v1::idled);
    QVERIFY(idle_spy.isValid());
    QSignalSpy resume_spy(notification.get(), &Wrapland::Client::idle_notification_v1::resumed);
    QVERIFY(resume_spy.isValid());

    // With timeout 0 is idle immediately.
    QVERIFY(idle_spy.wait());

    // now create inhibition on window
    std::unique_ptr<IdleInhibitor> inhibitor(
        Test::get_client().interfaces.idle_inhibit->createInhibitor(surface.get()));
    QVERIFY(inhibitor->isValid());

    // render the client
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);

    // this should inhibit our server object
    QCOMPARE(idle.inhibit_count, 1);

    // but not resume directly
    QVERIFY(!resume_spy.wait(200));

    // Activity should though.
    uint32_t time{};
    Test::pointer_button_pressed(BTN_LEFT, ++time);
    Test::pointer_button_released(BTN_LEFT, ++time);
    QVERIFY(resume_spy.wait());

    // With the inhibit no idle will be sent.
    QVERIFY(!idle_spy.wait(200));

    // deleting the object should uninhibit again
    inhibitor.reset();
    QVERIFY(idle_spy.wait());
    QCOMPARE(idle.inhibit_count, 0);

    // inhibit again and destroy window
    Test::get_client().interfaces.idle_inhibit->createInhibitor(surface.get(), surface.get());
    Test::pointer_button_pressed(BTN_LEFT, ++time);
    Test::pointer_button_released(BTN_LEFT, ++time);
    QVERIFY(resume_spy.wait());
    QVERIFY(!idle_spy.wait(200));
    QTRY_COMPARE(idle.inhibit_count, 1);

    shellSurface.reset();
    QVERIFY(Test::wait_for_destroyed(c));
    QCOMPARE(idle.inhibit_count, 0);
}

void TestIdleInhibition::testDontInhibitWhenNotOnCurrentDesktop()
{
    // This test verifies that the idle inhibitor object is not honored when
    // the associated surface is not on the current virtual desktop.

    auto& vd_manager = Test::app()->base.space->virtual_desktop_manager;
    vd_manager->setCount(2);
    QCOMPARE(vd_manager->count(), 2u);

    // Get reference to the idle interface.
    auto& idle = Test::app()->base.input->idle;
    QCOMPARE(idle.inhibit_count, 0);

    // Create the test client.
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);

    // Create the inhibitor object.
    std::unique_ptr<IdleInhibitor> inhibitor(
        Test::get_client().interfaces.idle_inhibit->createInhibitor(surface.get()));
    QVERIFY(inhibitor->isValid());

    // Render the client.
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);

    // The test client should be only on the first virtual desktop.
    QCOMPARE(c->topo.desktops.count(), 1);
    QCOMPARE(c->topo.desktops.constFirst(), vd_manager->desktops().first());

    // This should inhibit our server object.
    QCOMPARE(idle.inhibit_count, 1);

    // Switch to the second virtual desktop.
    vd_manager->setCurrent(2);

    // The surface is no longer visible, so the compositor don't have to honor the
    // idle inhibitor object.
    QCOMPARE(idle.inhibit_count, 0);

    // Switch back to the first virtual desktop.
    vd_manager->setCurrent(1);

    // The test client became visible again, so the compositor has to honor the idle
    // inhibitor object back again.
    QCOMPARE(idle.inhibit_count, 1);

    // Destroy the test client.
    shellSurface.reset();
    QVERIFY(Test::wait_for_destroyed(c));
    QCOMPARE(idle.inhibit_count, 0);
}

void TestIdleInhibition::testDontInhibitWhenMinimized()
{
    // This test verifies that the idle inhibitor object is not honored when the
    // associated surface is minimized.

    // Get reference to the idle interface.
    auto& idle = Test::app()->base.input->idle;
    QCOMPARE(idle.inhibit_count, 0);

    // Create the test client.
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);

    // Create the inhibitor object.
    std::unique_ptr<IdleInhibitor> inhibitor(
        Test::get_client().interfaces.idle_inhibit->createInhibitor(surface.get()));
    QVERIFY(inhibitor->isValid());

    // Render the client.
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);

    // This should inhibit our server object.
    QCOMPARE(idle.inhibit_count, 1);

    // Minimize the client, the idle inhibitor object should not be honored.
    win::set_minimized(c, true);
    QCOMPARE(idle.inhibit_count, 0);

    // Unminimize the client, the idle inhibitor object should be honored back again.
    win::set_minimized(c, false);
    QCOMPARE(idle.inhibit_count, 1);

    // Destroy the test client.
    shellSurface.reset();
    QVERIFY(Test::wait_for_destroyed(c));
    QCOMPARE(idle.inhibit_count, 0);
}

void TestIdleInhibition::testDontInhibitWhenUnmapped()
{
    // This test verifies that the idle inhibitor object is not honored by KWin
    // when the associated client is unmapped.

    // Get reference to the idle interface.
    auto& idle = Test::app()->base.input->idle;
    QCOMPARE(idle.inhibit_count, 0);

    // Create the test client.
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);

    // Create the inhibitor object.
    std::unique_ptr<IdleInhibitor> inhibitor(
        Test::get_client().interfaces.idle_inhibit->createInhibitor(surface.get()));
    QVERIFY(inhibitor->isValid());

    // Render the client.
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);

    // This should inhibit our server object.
    QCOMPARE(idle.inhibit_count, 1);

    // Unmap the client.
    QSignalSpy hiddenSpy(c->qobject.get(), &win::window_qobject::windowHidden);
    QVERIFY(hiddenSpy.isValid());
    surface->attachBuffer(Buffer::Ptr());
    surface->commit(Surface::CommitFlag::None);
    QVERIFY(hiddenSpy.wait());

    // The surface is no longer visible, so the compositor don't have to honor the
    // idle inhibitor object.
    QCOMPARE(idle.inhibit_count, 0);

    // Map the client.
    QSignalSpy windowShownSpy(c->qobject.get(), &win::window_qobject::windowShown);
    QVERIFY(windowShownSpy.isValid());
    Test::render(surface, QSize(100, 50), Qt::blue);
    QVERIFY(windowShownSpy.wait());

    // The test client became visible again, so the compositor has to honor the idle
    // inhibitor object back again.
    QCOMPARE(idle.inhibit_count, 1);

    // Destroy the test client.
    shellSurface.reset();
    QVERIFY(Test::wait_for_destroyed(c));
    QCOMPARE(idle.inhibit_count, 0);
}

void TestIdleInhibition::testDontInhibitWhenLeftCurrentDesktop()
{
    // This test verifies that the idle inhibitor object is not honored by KWin
    // when the associated surface leaves the current virtual desktop.

    auto& vd_manager = Test::app()->base.space->virtual_desktop_manager;
    vd_manager->setCount(2);
    QCOMPARE(vd_manager->count(), 2u);

    // Get reference to the idle interface.
    auto& idle = Test::app()->base.input->idle;
    QCOMPARE(idle.inhibit_count, 0);

    // Create the test client.
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);

    // Create the inhibitor object.
    std::unique_ptr<IdleInhibitor> inhibitor(
        Test::get_client().interfaces.idle_inhibit->createInhibitor(surface.get()));
    QVERIFY(inhibitor->isValid());

    // Render the client.
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);

    // The test client should be only on the first virtual desktop.
    QCOMPARE(c->topo.desktops.count(), 1);
    QCOMPARE(c->topo.desktops.constFirst(), vd_manager->desktops().first());

    // This should inhibit our server object.
    QCOMPARE(idle.inhibit_count, 1);

    // Let the client enter the second virtual desktop.
    win::enter_desktop(c, vd_manager->desktops().at(1));
    QCOMPARE(idle.inhibit_count, 1);

    // If the client leaves the first virtual desktop, then the associated idle
    // inhibitor object should not be honored.
    win::leave_desktop(c, vd_manager->desktops().at(0));
    QCOMPARE(idle.inhibit_count, 0);

    // If the client enters the first desktop, then the associated idle inhibitor
    // object should be honored back again.
    win::enter_desktop(c, vd_manager->desktops().at(0));
    QCOMPARE(idle.inhibit_count, 1);

    // Destroy the test client.
    shellSurface.reset();
    QVERIFY(Test::wait_for_destroyed(c));
    QCOMPARE(idle.inhibit_count, 0);
}

}

WAYLANDTEST_MAIN(KWin::TestIdleInhibition)
#include "idle_inhibition_test.moc"
