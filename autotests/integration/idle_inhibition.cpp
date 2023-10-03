/*
SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "win/actions.h"
#include "win/desktop_set.h"
#include "win/screen.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/idle_notify_v1.h>
#include <Wrapland/Client/idleinhibit.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>

#include <Wrapland/Server/display.h>
#include <Wrapland/Server/kde_idle.h>

using namespace Wrapland::Client;

namespace KWin::detail::test
{

TEST_CASE("idle inhibition", "[win]")
{
    test::setup setup("idle-inhibition");
    setup.start();
    setup.set_outputs(2);
    test_outputs_default();
    setup_wayland_connection(global_selection::idle_inhibition | global_selection::seat);

    SECTION("inhibit")
    {
        auto& idle = setup.base->input->idle;
        QCOMPARE(idle.inhibit_count, 0);

        // now create window
        std::unique_ptr<Surface> surface(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));

        auto notification = std::unique_ptr<Wrapland::Client::idle_notification_v1>(
            get_client().interfaces.idle_notifier->get_notification(
                0, get_client().interfaces.seat.get()));
        QVERIFY(notification->isValid());

        QSignalSpy idle_spy(notification.get(), &Wrapland::Client::idle_notification_v1::idled);
        QVERIFY(idle_spy.isValid());
        QSignalSpy resume_spy(notification.get(), &Wrapland::Client::idle_notification_v1::resumed);
        QVERIFY(resume_spy.isValid());

        // With timeout 0 is idle immediately.
        QVERIFY(idle_spy.wait());

        // now create inhibition on window
        std::unique_ptr<IdleInhibitor> inhibitor(
            get_client().interfaces.idle_inhibit->createInhibitor(surface.get()));
        QVERIFY(inhibitor->isValid());

        // render the client
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(c);

        // this should inhibit our server object
        QCOMPARE(idle.inhibit_count, 1);

        // but not resume directly
        QVERIFY(!resume_spy.wait(200));

        // Activity should though.
        uint32_t time{};
        pointer_button_pressed(BTN_LEFT, ++time);
        pointer_button_released(BTN_LEFT, ++time);
        QVERIFY(resume_spy.wait());

        // With the inhibit no idle will be sent.
        QVERIFY(!idle_spy.wait(200));

        // deleting the object should uninhibit again
        inhibitor.reset();
        QVERIFY(idle_spy.wait());
        QCOMPARE(idle.inhibit_count, 0);

        // inhibit again and destroy window
        get_client().interfaces.idle_inhibit->createInhibitor(surface.get(), surface.get());
        pointer_button_pressed(BTN_LEFT, ++time);
        pointer_button_released(BTN_LEFT, ++time);
        QVERIFY(resume_spy.wait());
        QVERIFY(!idle_spy.wait(200));
        QTRY_COMPARE(idle.inhibit_count, 1);

        shellSurface.reset();
        QVERIFY(wait_for_destroyed(c));
        QCOMPARE(idle.inhibit_count, 0);
    }

    SECTION("no inhibit on other subspace")
    {
        // This test verifies that the idle inhibitor object is not honored when
        // the associated surface is not on the current subspace.

        auto& vd_manager = setup.base->space->subspace_manager;
        vd_manager->setCount(2);
        QCOMPARE(vd_manager->count(), 2u);

        // Get reference to the idle interface.
        auto& idle = setup.base->input->idle;
        QCOMPARE(idle.inhibit_count, 0);

        // Create the test client.
        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);

        // Create the inhibitor object.
        std::unique_ptr<IdleInhibitor> inhibitor(
            get_client().interfaces.idle_inhibit->createInhibitor(surface.get()));
        QVERIFY(inhibitor->isValid());

        // Render the client.
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(c);

        // The test client should be only on the first subspace.
        QCOMPARE(c->topo.subspaces.size(), 1);
        QCOMPARE(c->topo.subspaces.front(), vd_manager->subspaces().front());

        // This should inhibit our server object.
        QCOMPARE(idle.inhibit_count, 1);

        // Switch to the second subspace.
        vd_manager->setCurrent(2);

        // The surface is no longer visible, so the compositor don't have to honor the
        // idle inhibitor object.
        QCOMPARE(idle.inhibit_count, 0);

        // Switch back to the first subspace.
        vd_manager->setCurrent(1);

        // The test client became visible again, so the compositor has to honor the idle
        // inhibitor object back again.
        QCOMPARE(idle.inhibit_count, 1);

        // Destroy the test client.
        shellSurface.reset();
        QVERIFY(wait_for_destroyed(c));
        QCOMPARE(idle.inhibit_count, 0);
    }

    SECTION("no inhibit minimized")
    {
        // This test verifies that the idle inhibitor object is not honored when the
        // associated surface is minimized.

        // Get reference to the idle interface.
        auto& idle = setup.base->input->idle;
        QCOMPARE(idle.inhibit_count, 0);

        // Create the test client.
        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);

        // Create the inhibitor object.
        std::unique_ptr<IdleInhibitor> inhibitor(
            get_client().interfaces.idle_inhibit->createInhibitor(surface.get()));
        QVERIFY(inhibitor->isValid());

        // Render the client.
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
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
        QVERIFY(wait_for_destroyed(c));
        QCOMPARE(idle.inhibit_count, 0);
    }

    SECTION("no inhibit unmapped")
    {
        // This test verifies that the idle inhibitor object is not honored by KWin
        // when the associated client is unmapped.

        // Get reference to the idle interface.
        auto& idle = setup.base->input->idle;
        QCOMPARE(idle.inhibit_count, 0);

        // Create the test client.
        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);

        // Create the inhibitor object.
        std::unique_ptr<IdleInhibitor> inhibitor(
            get_client().interfaces.idle_inhibit->createInhibitor(surface.get()));
        QVERIFY(inhibitor->isValid());

        // Render the client.
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
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
        render(surface, QSize(100, 50), Qt::blue);
        QVERIFY(windowShownSpy.wait());

        // The test client became visible again, so the compositor has to honor the idle
        // inhibitor object back again.
        QCOMPARE(idle.inhibit_count, 1);

        // Destroy the test client.
        shellSurface.reset();
        QVERIFY(wait_for_destroyed(c));
        QCOMPARE(idle.inhibit_count, 0);
    }

    SECTION("no inhibit left current subspace")
    {
        // This test verifies that the idle inhibitor object is not honored by KWin
        // when the associated surface leaves the current subspace.

        auto& vd_manager = setup.base->space->subspace_manager;
        vd_manager->setCount(2);
        QCOMPARE(vd_manager->count(), 2u);

        // Get reference to the idle interface.
        auto& idle = setup.base->input->idle;
        QCOMPARE(idle.inhibit_count, 0);

        // Create the test client.
        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);

        // Create the inhibitor object.
        std::unique_ptr<IdleInhibitor> inhibitor(
            get_client().interfaces.idle_inhibit->createInhibitor(surface.get()));
        QVERIFY(inhibitor->isValid());

        // Render the client.
        auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(c);

        // The test client should be only on the first subspace.
        QCOMPARE(c->topo.subspaces.size(), 1);
        QCOMPARE(c->topo.subspaces.front(), vd_manager->subspaces().front());

        // This should inhibit our server object.
        QCOMPARE(idle.inhibit_count, 1);

        // Let the client enter the second subspace.
        win::enter_subspace(*c, vd_manager->subspaces().at(1));
        QCOMPARE(idle.inhibit_count, 1);

        // If the client leaves the first subspace, then the associated idle
        // inhibitor object should not be honored.
        win::leave_subspace(*c, vd_manager->subspaces().at(0));
        QCOMPARE(idle.inhibit_count, 0);

        // If the client enters the first subspace, then the associated idle inhibitor
        // object should be honored back again.
        win::enter_subspace(*c, vd_manager->subspaces().at(0));
        QCOMPARE(idle.inhibit_count, 1);

        // Destroy the test client.
        shellSurface.reset();
        QVERIFY(wait_for_destroyed(c));
        QCOMPARE(idle.inhibit_count, 0);
    }
}

}
