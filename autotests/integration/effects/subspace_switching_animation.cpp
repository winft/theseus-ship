/*
SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>
#include <catch2/generators/catch_generators.hpp>

namespace KWin::detail::test
{

TEST_CASE("subspace switching animation", "[effect]")
{
    // This test verifies that subspace switching animation effects actually
    // try to animate switching between subspaces.
    using namespace Wrapland::Client;

    auto effectName = GENERATE(QString("cubeslide"), QString("fadedesktop"), QString("slide"));

    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));
    qputenv("KWIN_EFFECTS_FORCE_ANIMATIONS", QByteArrayLiteral("1"));
    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());

    test::setup setup("subspace-switching-animation");
    auto config = setup.base->config.main;
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    auto const builtinNames = render::effect_loader(*setup.base->render).listOfKnownEffects();

    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }

    config->sync();

    setup.start();

    auto& scene = setup.base->render->scene;
    QVERIFY(scene);
    REQUIRE(scene->isOpenGl());

    // We need at least 2 subspaces for the test.
    auto& subs = setup.base->space->subspace_manager;
    win::subspace_manager_set_count(*subs, 2);
    QCOMPARE(win::subspaces_get_current_x11id(*subs), 1u);
    QCOMPARE(subs->subspaces.size(), 2u);

    setup_wayland_connection();

    // The Fade Desktop effect will do nothing if there are no clients to fade,
    // so we have to create a dummy test client.
    std::unique_ptr<Surface> surface(create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QCOMPARE(client->topo.subspaces.size(), 1);
    QCOMPARE(client->topo.subspaces.front(), subs->subspaces.front());

    // Load effect that will be tested.
    auto& effectsImpl = setup.base->render->effects;
    QVERIFY(effectsImpl);
    QVERIFY(effectsImpl->loadEffect(effectName));
    QCOMPARE(effectsImpl->loadedEffects().count(), 1);
    QCOMPARE(effectsImpl->loadedEffects().constFirst(), effectName);
    Effect* effect = effectsImpl->findEffect(effectName);
    QVERIFY(effect);
    QVERIFY(!effect->isActive());

    // Switch to the second subspace.
    win::subspaces_set_current(*subs, 2u);
    QCOMPARE(win::subspaces_get_current_x11id(*subs), 2u);
    QVERIFY(effect->isActive());
    QCOMPARE(effects->activeFullScreenEffect(), effect);

    // Eventually, the animation will be complete.
    QTRY_VERIFY(!effect->isActive());
    QTRY_COMPARE(effects->activeFullScreenEffect(), nullptr);

    // Destroy the test client.
    surface.reset();
    QVERIFY(wait_for_destroyed(client));
}

}
