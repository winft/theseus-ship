/*
SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "win/activation.h"
#include "win/net.h"
#include "win/space.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/plasmashell.h>
#include <Wrapland/Client/surface.h>

using namespace Wrapland::Client;

namespace KWin::detail::test
{

TEST_CASE("showing desktop", "[win]")
{
    test::setup setup("showing-desktop");
    setup.start();
    Test::setup_wayland_connection(Test::global_selection::plasma_shell);

    SECTION("restore focus")
    {
        std::unique_ptr<Surface> surface1(Test::create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
        QVERIFY(surface1);
        QVERIFY(shellSurface1);

        auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
        std::unique_ptr<Surface> surface2(Test::create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
        QVERIFY(surface2);
        QVERIFY(shellSurface2);

        auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
        QVERIFY(client1 != client2);

        QCOMPARE(Test::get_wayland_window(setup.base->space->stacking.active), client2);
        win::toggle_show_desktop(*setup.base->space);
        QVERIFY(setup.base->space->showing_desktop);
        win::toggle_show_desktop(*setup.base->space);
        QVERIFY(!setup.base->space->showing_desktop);

        QVERIFY(Test::get_wayland_window(setup.base->space->stacking.active));
        QCOMPARE(Test::get_wayland_window(setup.base->space->stacking.active), client2);
    }

    SECTION("restore focus with desktop window")
    {
        // first create a desktop window

        std::unique_ptr<Surface> desktopSurface(Test::create_surface());
        QVERIFY(desktopSurface);
        std::unique_ptr<XdgShellToplevel> desktopShellSurface(
            Test::create_xdg_shell_toplevel(desktopSurface));
        QVERIFY(desktopShellSurface);
        std::unique_ptr<PlasmaShellSurface> plasmaSurface(
            Test::get_client().interfaces.plasma_shell->createSurface(desktopSurface.get()));
        QVERIFY(plasmaSurface);
        plasmaSurface->setRole(PlasmaShellSurface::Role::Desktop);

        auto desktop = Test::render_and_wait_for_shown(desktopSurface, QSize(100, 50), Qt::blue);
        QVERIFY(desktop);
        QVERIFY(win::is_desktop(desktop));

        // now create some windows
        std::unique_ptr<Surface> surface1(Test::create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface1(Test::create_xdg_shell_toplevel(surface1));
        QVERIFY(surface1);
        QVERIFY(shellSurface1);

        auto client1 = Test::render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
        std::unique_ptr<Surface> surface2(Test::create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface2(Test::create_xdg_shell_toplevel(surface2));
        QVERIFY(surface2);
        QVERIFY(shellSurface2);

        auto client2 = Test::render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
        QVERIFY(client1 != client2);

        QCOMPARE(Test::get_wayland_window(setup.base->space->stacking.active), client2);
        win::toggle_show_desktop(*setup.base->space);
        QVERIFY(setup.base->space->showing_desktop);
        QCOMPARE(Test::get_wayland_window(setup.base->space->stacking.active), desktop);
        win::toggle_show_desktop(*setup.base->space);
        QVERIFY(!setup.base->space->showing_desktop);

        QVERIFY(Test::get_wayland_window(setup.base->space->stacking.active));
        QCOMPARE(Test::get_wayland_window(setup.base->space->stacking.active), client2);
    }
}

}
