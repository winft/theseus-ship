/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <KDecoration2/Decoration>
#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>

namespace KWin::detail::test
{

TEST_CASE("no crash cancel animation", "[render]")
{
    test::setup setup("no-crash-cancel-animation");
    setup.start();

    REQUIRE(setup.base->render);
    REQUIRE(effects);

    setup_wayland_connection();

    // load a scripted effect which deletes animation data
    auto effect = scripting::effect::create(QStringLiteral("crashy"),
                                            QFINDTESTDATA("data/anim-data-delete-effect/effect.js"),
                                            10,
                                            QString(),
                                            *effects,
                                            *setup.base->render);
    QVERIFY(effect);

    setup.base->render->effects->loader->effectLoaded(effect, "crashy");
    QVERIFY(setup.base->render->effects->isEffectLoaded(QStringLiteral("crashy")));

    using namespace Wrapland::Client;

    // create a window
    auto surface = std::unique_ptr<Wrapland::Client::Surface>(create_surface());
    QVERIFY(surface);
    auto shellSurface
        = std::unique_ptr<Wrapland::Client::XdgShellToplevel>(create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);

    // let's render
    auto c = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(get_wayland_window(setup.base->space->stacking.active), c);

    // make sure we animate
    QTest::qWait(200);

    // wait for the window to be passed to Deleted
    QSignalSpy windowDeletedSpy(c->qobject.get(), &win::window_qobject::closed);
    QVERIFY(windowDeletedSpy.isValid());

    surface.reset();

    QVERIFY(windowDeletedSpy.wait());

    // make sure we animate
    QTest::qWait(200);
}

}
