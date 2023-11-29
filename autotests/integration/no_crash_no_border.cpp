/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <KDecoration2/Decoration>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdgdecoration.h>
#include <linux/input.h>

namespace KWin::detail::test
{

TEST_CASE("no crash no border", "[win]")
{
    // Create a window and ensure that this doesn't crash.
    using namespace Wrapland::Client;

    // this test needs to enforce OpenGL compositing to get into the crashy condition
    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));

    test::setup setup("no-crash-no-border");

    auto config = setup.base->config.main;
    config->group("org.kde.kdecoration2").writeEntry("NoPlugin", true);
    config->sync();

    setup.start();
    setup.set_outputs(2);
    test_outputs_default();

    auto& scene = setup.base->mod.render->scene;
    QVERIFY(scene);
    REQUIRE(scene->isOpenGl());

    setup_wayland_connection(global_selection::xdg_decoration);
    cursor()->set_pos(QPoint(640, 512));

    std::unique_ptr<Surface> surface(create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(
        create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly));
    QVERIFY(shellSurface);

    auto deco = get_client().interfaces.xdg_decoration->getToplevelDecoration(shellSurface.get(),
                                                                              shellSurface.get());
    QSignalSpy decoSpy(deco, &XdgDecoration::modeChanged);
    QVERIFY(decoSpy.isValid());
    deco->setMode(XdgDecoration::Mode::ServerSide);
    QCOMPARE(deco->mode(), XdgDecoration::Mode::ClientSide);
    init_xdg_shell_toplevel(surface, shellSurface);

    // Without server-side decoration available the mode set by the compositor will be client-side.
    QCOMPARE(deco->mode(), XdgDecoration::Mode::ClientSide);

    // let's render
    auto c = render_and_wait_for_shown(surface, QSize(500, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(get_wayland_window(setup.base->mod.space->stacking.active), c);
    QVERIFY(!win::decoration(c));
}

}
