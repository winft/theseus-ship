/*
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "generic_scene_opengl_test.h"
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "render/compositor.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/subsurface.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>

namespace KWin::detail::test
{

TEST_CASE("buffer size change", "[render]")
{
    auto setup = generic_scene_opengl_get_setup("buffer-size-change", "O2");
    setup_wayland_connection();

    SECTION("shm")
    {
        // Verifies that an SHM buffer size change is handled correctly.

        using namespace Wrapland::Client;

        auto surface = create_surface();
        QVERIFY(surface);

        auto shellSurface = create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);

        // set buffer size
        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);

        // add a first repaint
        render::full_repaint(*setup->base->render->compositor);

        // now change buffer size
        render(surface, QSize(30, 10), Qt::red);

        QSignalSpy damagedSpy(client->qobject.get(), &win::window_qobject::damaged);
        QVERIFY(damagedSpy.isValid());
        QVERIFY(damagedSpy.wait());
        render::full_repaint(*setup->base->render->compositor);
    }

    SECTION("shm on subsurface")
    {
        using namespace Wrapland::Client;

        // setup parent surface
        auto parentSurface = create_surface();
        QVERIFY(parentSurface);
        auto shellSurface = create_xdg_shell_toplevel(parentSurface);
        QVERIFY(shellSurface);

        // setup sub surface
        auto surface = create_surface();
        QVERIFY(surface);
        std::unique_ptr<SubSurface> subSurface(create_subsurface(surface, parentSurface));
        QVERIFY(subSurface);

        // set buffer sizes
        render(surface, QSize(30, 10), Qt::red);
        auto parent = render_and_wait_for_shown(parentSurface, QSize(100, 50), Qt::blue);
        QVERIFY(parent);

        // add a first repaint
        render::full_repaint(*setup->base->render->compositor);

        // change buffer size of sub surface
        QSignalSpy damagedParentSpy(parent->qobject.get(), &win::window_qobject::damaged);
        QVERIFY(damagedParentSpy.isValid());
        render(surface, QSize(20, 10), Qt::red);
        parentSurface->commit(Surface::CommitFlag::None);

        QVERIFY(damagedParentSpy.wait());
        QTRY_COMPARE(damagedParentSpy.count(), 2);

        // add a second repaint
        render::full_repaint(*setup->base->render->compositor);
    }
}

}
