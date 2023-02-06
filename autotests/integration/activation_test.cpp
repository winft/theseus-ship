/*
SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "input/cursor.h"
#include "win/active_window.h"
#include "win/control.h"
#include "win/move.h"
#include "win/space.h"
#include "win/stacking_order.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/surface.h>

namespace KWin::detail::test
{

TEST_CASE("activation", "[win]")
{
    test::setup setup("activation");
    setup.start();
    setup.set_outputs(2);
    Test::test_outputs_default();
    Test::setup_wayland_connection();

    auto stackScreensHorizontally = [&]() {
        auto const geometries = std::vector<QRect>{{0, 0, 1280, 1024}, {1280, 0, 1280, 1024}};
        setup.set_outputs(geometries);
    };

    auto stackScreensVertically = [&]() {
        auto const geometries = std::vector<QRect>{{0, 0, 1280, 1024}, {0, 1024, 1280, 1024}};
        setup.set_outputs(geometries);
    };

    SECTION("switch to left window")
    {
        // Verifies that "Switch to Window to the Left" shortcut works.
        using namespace Wrapland::Client;

        // Prepare the test environment.
        stackScreensHorizontally();

        // Create several clients on the left screen.
        auto surface1 = Test::create_surface();
        auto shellSurface1 = Test::create_xdg_shell_toplevel(surface1);
        auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
        REQUIRE(client1);
        REQUIRE(client1->control->active);

        auto surface2 = Test::create_surface();
        auto shellSurface2 = Test::create_xdg_shell_toplevel(surface2);
        auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
        REQUIRE(client2);
        REQUIRE(client2->control->active);

        win::move(client1, QPoint(300, 200));
        win::move(client2, QPoint(500, 200));

        // Create several clients on the right screen.
        auto surface3 = Test::create_surface();
        auto shellSurface3 = Test::create_xdg_shell_toplevel(surface3);
        auto client3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
        REQUIRE(client3);
        REQUIRE(client3->control->active);

        auto surface4 = Test::create_surface();
        auto shellSurface4 = Test::create_xdg_shell_toplevel(surface4);
        auto client4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);
        REQUIRE(client4);
        REQUIRE(client4->control->active);

        win::move(client3, QPoint(1380, 200));
        win::move(client4, QPoint(1580, 200));

        // Switch to window to the left.
        win::activate_window_direction(*setup.base->space, win::direction::west);
        REQUIRE(client3->control->active);

        // Switch to window to the left.
        win::activate_window_direction(*setup.base->space, win::direction::west);
        REQUIRE(client2->control->active);

        // Switch to window to the left.
        win::activate_window_direction(*setup.base->space, win::direction::west);
        REQUIRE(client1->control->active);

        // Switch to window to the left.
        win::activate_window_direction(*setup.base->space, win::direction::west);
        REQUIRE(client4->control->active);

        // Destroy all clients.
        shellSurface1.reset();
        REQUIRE(Test::wait_for_destroyed(client1));
        shellSurface2.reset();
        REQUIRE(Test::wait_for_destroyed(client2));
        shellSurface3.reset();
        REQUIRE(Test::wait_for_destroyed(client3));
        shellSurface4.reset();
        REQUIRE(Test::wait_for_destroyed(client4));
    }

    SECTION("switch to right window")
    {
        // Verifies that "Switch to Window to the Right" shortcut works.

        using namespace Wrapland::Client;

        // Prepare the test environment.
        stackScreensHorizontally();

        // Create several clients on the left screen.
        auto surface1 = Test::create_surface();
        auto shellSurface1 = Test::create_xdg_shell_toplevel(surface1);
        auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
        REQUIRE(client1);
        REQUIRE(client1->control->active);

        auto surface2 = Test::create_surface();
        auto shellSurface2 = Test::create_xdg_shell_toplevel(surface2);
        auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
        REQUIRE(client2);
        REQUIRE(client2->control->active);

        win::move(client1, QPoint(300, 200));
        win::move(client2, QPoint(500, 200));

        // Create several clients on the right screen.
        auto surface3 = Test::create_surface();
        auto shellSurface3 = Test::create_xdg_shell_toplevel(surface3);
        auto client3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
        REQUIRE(client3);
        REQUIRE(client3->control->active);

        auto surface4 = Test::create_surface();
        auto shellSurface4 = Test::create_xdg_shell_toplevel(surface4);
        auto client4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);
        REQUIRE(client4);
        REQUIRE(client4->control->active);

        win::move(client3, QPoint(1380, 200));
        win::move(client4, QPoint(1580, 200));

        // Switch to window to the right.
        win::activate_window_direction(*setup.base->space, win::direction::east);
        REQUIRE(client1->control->active);

        // Switch to window to the right.
        win::activate_window_direction(*setup.base->space, win::direction::east);
        REQUIRE(client2->control->active);

        // Switch to window to the right.
        win::activate_window_direction(*setup.base->space, win::direction::east);
        REQUIRE(client3->control->active);

        // Switch to window to the right.
        win::activate_window_direction(*setup.base->space, win::direction::east);
        REQUIRE(client4->control->active);

        // Destroy all clients.
        shellSurface1.reset();
        REQUIRE(Test::wait_for_destroyed(client1));
        shellSurface2.reset();
        REQUIRE(Test::wait_for_destroyed(client2));
        shellSurface3.reset();
        REQUIRE(Test::wait_for_destroyed(client3));
        shellSurface4.reset();
        REQUIRE(Test::wait_for_destroyed(client4));
    }

    SECTION("switch to above window")
    {
        // Verifies that "Switch to Window Above" shortcut works.

        using namespace Wrapland::Client;

        // Prepare the test environment.
        stackScreensVertically();

        // Create several clients on the top screen.
        auto surface1 = Test::create_surface();
        auto shellSurface1 = Test::create_xdg_shell_toplevel(surface1);
        auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
        REQUIRE(client1);
        REQUIRE(client1->control->active);

        auto surface2 = Test::create_surface();
        auto shellSurface2 = Test::create_xdg_shell_toplevel(surface2);
        auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
        REQUIRE(client2);
        REQUIRE(client2->control->active);

        win::move(client1, QPoint(200, 300));
        win::move(client2, QPoint(200, 500));

        // Create several clients on the bottom screen.
        auto surface3 = Test::create_surface();
        auto shellSurface3 = Test::create_xdg_shell_toplevel(surface3);
        auto client3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
        REQUIRE(client3);
        REQUIRE(client3->control->active);

        auto surface4 = Test::create_surface();
        auto shellSurface4 = Test::create_xdg_shell_toplevel(surface4);
        auto client4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);
        REQUIRE(client4);
        REQUIRE(client4->control->active);

        win::move(client3, QPoint(200, 1224));
        win::move(client4, QPoint(200, 1424));

        // Switch to window above.
        win::activate_window_direction(*setup.base->space, win::direction::north);
        REQUIRE(client3->control->active);

        // Switch to window above.
        win::activate_window_direction(*setup.base->space, win::direction::north);
        REQUIRE(client2->control->active);

        // Switch to window above.
        win::activate_window_direction(*setup.base->space, win::direction::north);
        REQUIRE(client1->control->active);

        // Switch to window above.
        win::activate_window_direction(*setup.base->space, win::direction::north);
        REQUIRE(client4->control->active);

        // Destroy all clients.
        shellSurface1.reset();
        REQUIRE(Test::wait_for_destroyed(client1));
        shellSurface2.reset();
        REQUIRE(Test::wait_for_destroyed(client2));
        shellSurface3.reset();
        REQUIRE(Test::wait_for_destroyed(client3));
        shellSurface4.reset();
        REQUIRE(Test::wait_for_destroyed(client4));
    }

    SECTION("switch to bottom window")
    {
        // Verifies that "Switch to Window Bottom" shortcut works.

        using namespace Wrapland::Client;

        // Prepare the test environment.
        stackScreensVertically();

        // Create several clients on the top screen.
        auto surface1 = Test::create_surface();
        auto shellSurface1 = Test::create_xdg_shell_toplevel(surface1);
        auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
        REQUIRE(client1);
        REQUIRE(client1->control->active);

        auto surface2 = Test::create_surface();
        auto shellSurface2 = Test::create_xdg_shell_toplevel(surface2);
        auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
        REQUIRE(client2);
        REQUIRE(client2->control->active);

        win::move(client1, QPoint(200, 300));
        win::move(client2, QPoint(200, 500));

        // Create several clients on the bottom screen.
        auto surface3 = Test::create_surface();
        auto shellSurface3 = Test::create_xdg_shell_toplevel(surface3);
        auto client3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
        REQUIRE(client3);
        REQUIRE(client3->control->active);

        auto surface4 = Test::create_surface();
        auto shellSurface4 = Test::create_xdg_shell_toplevel(surface4);
        auto client4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);
        REQUIRE(client4);
        REQUIRE(client4->control->active);

        win::move(client3, QPoint(200, 1224));
        win::move(client4, QPoint(200, 1424));

        // Switch to window below.
        win::activate_window_direction(*setup.base->space, win::direction::south);
        REQUIRE(client1->control->active);

        // Switch to window below.
        win::activate_window_direction(*setup.base->space, win::direction::south);
        REQUIRE(client2->control->active);

        // Switch to window below.
        win::activate_window_direction(*setup.base->space, win::direction::south);
        REQUIRE(client3->control->active);

        // Switch to window below.
        win::activate_window_direction(*setup.base->space, win::direction::south);
        REQUIRE(client4->control->active);

        // Destroy all clients.
        shellSurface1.reset();
        REQUIRE(Test::wait_for_destroyed(client1));
        shellSurface2.reset();
        REQUIRE(Test::wait_for_destroyed(client2));
        shellSurface3.reset();
        REQUIRE(Test::wait_for_destroyed(client3));
        shellSurface4.reset();
        REQUIRE(Test::wait_for_destroyed(client4));
    }

    SECTION("switch to top-most maximized window")
    {
        // Verifies that we switch to the top-most maximized client, i.e.
        // the one that user sees at the moment. See bug 411356.

        using namespace Wrapland::Client;

        // Prepare the test environment.
        stackScreensHorizontally();

        // Create several maximized clients on the left screen.
        auto surface1 = Test::create_surface();
        auto shellSurface1 = Test::create_xdg_shell_toplevel(surface1);
        auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
        REQUIRE(client1);
        REQUIRE(client1->control->active);

        QSignalSpy configureRequestedSpy1(shellSurface1.get(), &XdgShellToplevel::configured);
        REQUIRE(configureRequestedSpy1.isValid());

        REQUIRE(configureRequestedSpy1.wait());
        win::active_window_maximize(*setup.base->space);

        REQUIRE(configureRequestedSpy1.wait());

        QSignalSpy geometryChangedSpy1(client1->qobject.get(),
                                       &win::window_qobject::frame_geometry_changed);
        REQUIRE(geometryChangedSpy1.isValid());

        shellSurface1->ackConfigure(configureRequestedSpy1.last().front().value<quint32>());
        Test::render(surface1, shellSurface1->get_configure_data().size, Qt::red);

        REQUIRE(geometryChangedSpy1.wait());
        REQUIRE(client1->maximizeMode() == win::maximize_mode::full);

        auto surface2 = Test::create_surface();
        auto shellSurface2 = Test::create_xdg_shell_toplevel(surface2);
        auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
        REQUIRE(client2);
        REQUIRE(client2->control->active);

        QSignalSpy configureRequestedSpy2(shellSurface2.get(), &XdgShellToplevel::configured);
        REQUIRE(configureRequestedSpy2.isValid());

        REQUIRE(configureRequestedSpy2.wait());
        win::active_window_maximize(*setup.base->space);

        REQUIRE(configureRequestedSpy2.wait());

        QSignalSpy geometryChangedSpy2(client2->qobject.get(),
                                       &win::window_qobject::frame_geometry_changed);
        REQUIRE(geometryChangedSpy2.isValid());

        shellSurface2->ackConfigure(configureRequestedSpy2.last().front().value<quint32>());
        Test::render(surface2, shellSurface2->get_configure_data().size, Qt::red);

        REQUIRE(geometryChangedSpy2.wait());

        auto const stackingOrder = setup.base->space->stacking.order.stack;
        REQUIRE(index_of(stackingOrder, Test::space::window_t(client1))
                < index_of(stackingOrder, Test::space::window_t(client2)));
        REQUIRE(client1->maximizeMode() == win::maximize_mode::full);
        REQUIRE(client2->maximizeMode() == win::maximize_mode::full);

        // Create several clients on the right screen.
        auto surface3 = Test::create_surface();
        auto shellSurface3 = Test::create_xdg_shell_toplevel(surface3);
        auto client3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
        REQUIRE(client3);
        REQUIRE(client3->control->active);

        auto surface4 = Test::create_surface();
        auto shellSurface4 = Test::create_xdg_shell_toplevel(surface4);
        auto client4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);
        REQUIRE(client4);
        REQUIRE(client4->control->active);

        win::move(client3, QPoint(1380, 200));
        win::move(client4, QPoint(1580, 200));

        // Switch to window to the left.
        win::activate_window_direction(*setup.base->space, win::direction::west);
        REQUIRE(client3->control->active);

        // Switch to window to the left.
        win::activate_window_direction(*setup.base->space, win::direction::west);
        REQUIRE(client2->control->active);

        // Switch to window to the left.
        win::activate_window_direction(*setup.base->space, win::direction::west);
        REQUIRE(client4->control->active);

        // Destroy all clients.
        shellSurface1.reset();
        REQUIRE(Test::wait_for_destroyed(client1));
        shellSurface2.reset();
        REQUIRE(Test::wait_for_destroyed(client2));
        shellSurface3.reset();
        REQUIRE(Test::wait_for_destroyed(client3));
        shellSurface4.reset();
        REQUIRE(Test::wait_for_destroyed(client4));
    }

    SECTION("switch to top-most fullscreen window")
    {
        // Verifies that we switch to the top-most fullscreen fullscreen, i.e.
        // the one that user sees at the moment. See bug 411356.

        using namespace Wrapland::Client;

        // Prepare the test environment.
        stackScreensVertically();

        // Create several maximized clients on the top screen.
        auto surface1 = Test::create_surface();
        auto shellSurface1 = Test::create_xdg_shell_toplevel(surface1);
        auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
        REQUIRE(client1);
        REQUIRE(client1->control->active);

        QSignalSpy configureRequestedSpy1(shellSurface1.get(), &XdgShellToplevel::configured);
        REQUIRE(configureRequestedSpy1.isValid());

        REQUIRE(configureRequestedSpy1.wait());
        win::active_window_set_fullscreen(*setup.base->space);

        REQUIRE(configureRequestedSpy1.wait());

        QSignalSpy geometryChangedSpy1(client1->qobject.get(),
                                       &win::window_qobject::frame_geometry_changed);
        REQUIRE(geometryChangedSpy1.isValid());

        shellSurface1->ackConfigure(configureRequestedSpy1.last().front().value<quint32>());
        Test::render(surface1, shellSurface1->get_configure_data().size, Qt::red);
        REQUIRE(geometryChangedSpy1.wait());

        auto surface2 = Test::create_surface();
        auto shellSurface2 = Test::create_xdg_shell_toplevel(surface2);
        auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
        REQUIRE(client2);
        REQUIRE(client2->control->active);

        QSignalSpy configureRequestedSpy2(shellSurface2.get(), &XdgShellToplevel::configured);
        REQUIRE(configureRequestedSpy2.isValid());

        REQUIRE(configureRequestedSpy2.wait());
        win::active_window_set_fullscreen(*setup.base->space);

        REQUIRE(configureRequestedSpy2.wait());

        QSignalSpy geometryChangedSpy2(client2->qobject.get(),
                                       &win::window_qobject::frame_geometry_changed);
        REQUIRE(geometryChangedSpy2.isValid());

        shellSurface2->ackConfigure(configureRequestedSpy2.last().front().value<quint32>());
        Test::render(surface2, shellSurface2->get_configure_data().size, Qt::red);

        REQUIRE(geometryChangedSpy2.wait());

        auto const stackingOrder = setup.base->space->stacking.order.stack;
        REQUIRE(index_of(stackingOrder, Test::space::window_t(client1))
                < index_of(stackingOrder, Test::space::window_t(client2)));
        REQUIRE(client1->control->fullscreen);
        REQUIRE(client2->control->fullscreen);

        // Create several clients on the bottom screen.
        auto surface3 = Test::create_surface();
        auto shellSurface3 = Test::create_xdg_shell_toplevel(surface3);
        auto client3 = Test::render_and_wait_for_shown(surface3, QSize(100, 50), Qt::blue);
        REQUIRE(client3);
        REQUIRE(client3->control->active);

        auto surface4 = Test::create_surface();
        auto shellSurface4 = Test::create_xdg_shell_toplevel(surface4);
        auto client4 = Test::render_and_wait_for_shown(surface4, QSize(100, 50), Qt::blue);
        REQUIRE(client4);
        REQUIRE(client4->control->active);

        win::move(client3, QPoint(200, 1224));
        win::move(client4, QPoint(200, 1424));

        // Switch to window above.
        win::activate_window_direction(*setup.base->space, win::direction::north);
        REQUIRE(client3->control->active);

        // Switch to window above.
        win::activate_window_direction(*setup.base->space, win::direction::north);
        REQUIRE(client2->control->active);

        // Switch to window above.
        win::activate_window_direction(*setup.base->space, win::direction::north);
        REQUIRE(client4->control->active);

        // Destroy all clients.
        shellSurface1.reset();
        REQUIRE(Test::wait_for_destroyed(client1));
        shellSurface2.reset();
        REQUIRE(Test::wait_for_destroyed(client2));
        shellSurface3.reset();
        REQUIRE(Test::wait_for_destroyed(client3));
        shellSurface4.reset();
        REQUIRE(Test::wait_for_destroyed(client4));
    }
}

}
