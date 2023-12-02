/*
SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <Wrapland/Client/plasmashell.h>
#include <Wrapland/Client/surface.h>
#include <catch2/generators/catch_generators.hpp>

using namespace Wrapland::Client;

namespace KWin::detail::test
{

TEST_CASE("showing desktop", "[win]")
{
#if USE_XWL
    auto operation_mode = GENERATE(base::operation_mode::wayland, base::operation_mode::xwayland);
#else
    auto operation_mode = GENERATE(base::operation_mode::wayland);
#endif

    test::setup setup("showing-desktop", operation_mode);
    setup.start();
    setup_wayland_connection(global_selection::plasma_shell);

    SECTION("restore focus")
    {
        std::unique_ptr<Surface> surface1(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface1(create_xdg_shell_toplevel(surface1));
        QVERIFY(surface1);
        QVERIFY(shellSurface1);

        auto client1 = render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
        std::unique_ptr<Surface> surface2(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface2(create_xdg_shell_toplevel(surface2));
        QVERIFY(surface2);
        QVERIFY(shellSurface2);

        auto client2 = render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
        QVERIFY(client1 != client2);

        QCOMPARE(get_wayland_window(setup.base->mod.space->stacking.active), client2);
        win::toggle_show_desktop(*setup.base->mod.space);
        QVERIFY(setup.base->mod.space->showing_desktop);
        win::toggle_show_desktop(*setup.base->mod.space);
        QVERIFY(!setup.base->mod.space->showing_desktop);

        QVERIFY(get_wayland_window(setup.base->mod.space->stacking.active));
        QCOMPARE(get_wayland_window(setup.base->mod.space->stacking.active), client2);
    }

    SECTION("restore focus with desktop window")
    {
        // first create a desktop window

        std::unique_ptr<Surface> desktopSurface(create_surface());
        QVERIFY(desktopSurface);
        std::unique_ptr<XdgShellToplevel> desktopShellSurface(
            create_xdg_shell_toplevel(desktopSurface));
        QVERIFY(desktopShellSurface);
        std::unique_ptr<PlasmaShellSurface> plasmaSurface(
            get_client().interfaces.plasma_shell->createSurface(desktopSurface.get()));
        QVERIFY(plasmaSurface);
        plasmaSurface->setRole(PlasmaShellSurface::Role::Desktop);

        auto desktop = render_and_wait_for_shown(desktopSurface, QSize(100, 50), Qt::blue);
        QVERIFY(desktop);
        QVERIFY(win::is_desktop(desktop));

        // now create some windows
        std::unique_ptr<Surface> surface1(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface1(create_xdg_shell_toplevel(surface1));
        QVERIFY(surface1);
        QVERIFY(shellSurface1);

        auto client1 = render_and_wait_for_shown(surface1, QSize(100, 50), Qt::blue);
        std::unique_ptr<Surface> surface2(create_surface());
        std::unique_ptr<XdgShellToplevel> shellSurface2(create_xdg_shell_toplevel(surface2));
        QVERIFY(surface2);
        QVERIFY(shellSurface2);

        auto client2 = render_and_wait_for_shown(surface2, QSize(100, 50), Qt::blue);
        QVERIFY(client1 != client2);

        QCOMPARE(get_wayland_window(setup.base->mod.space->stacking.active), client2);
        win::toggle_show_desktop(*setup.base->mod.space);
        QVERIFY(setup.base->mod.space->showing_desktop);
        QCOMPARE(get_wayland_window(setup.base->mod.space->stacking.active), desktop);
        win::toggle_show_desktop(*setup.base->mod.space);
        QVERIFY(!setup.base->mod.space->showing_desktop);

        QVERIFY(get_wayland_window(setup.base->mod.space->stacking.active));
        QCOMPARE(get_wayland_window(setup.base->mod.space->stacking.active), client2);
    }
}

}
