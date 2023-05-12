/*
SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "render/compositor.h"
#include "render/effect_loader.h"
#include "render/effects.h"
#include "render/scene.h"
#include "win/space.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>
#include <catch2/generators/catch_generators.hpp>

namespace KWin::detail::test
{

TEST_CASE("desktop switching animation", "[effect]")
{
    // This test verifies that virtual desktop switching animation effects actually
    // try to animate switching between desktops.
    using namespace Wrapland::Client;

    auto effectName = GENERATE(QString("cubeslide"), QString("fadedesktop"), QString("slide"));

    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));
    qputenv("KWIN_EFFECTS_FORCE_ANIMATIONS", QByteArrayLiteral("1"));
    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());

    test::setup setup("desktop-switching-animation");
    auto config = setup.base->config.main;
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    auto const builtinNames
        = render::effect_loader(*effects, *setup.base->render).listOfKnownEffects();

    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }

    config->sync();

    setup.start();

    auto& scene = setup.base->render->compositor->scene;
    QVERIFY(scene);
    QCOMPARE(scene->compositingType(), OpenGLCompositing);

    // We need at least 2 virtual desktops for the test.
    auto& vd_manager = setup.base->space->virtual_desktop_manager;
    vd_manager->setCount(2);
    QCOMPARE(vd_manager->current(), 1u);
    QCOMPARE(vd_manager->count(), 2u);

    setup_wayland_connection();

    // The Fade Desktop effect will do nothing if there are no clients to fade,
    // so we have to create a dummy test client.
    std::unique_ptr<Surface> surface(create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QCOMPARE(client->topo.desktops.count(), 1);
    QCOMPARE(client->topo.desktops.constFirst(), vd_manager->desktops().first());

    // Load effect that will be tested.
    auto& effectsImpl = setup.base->render->compositor->effects;
    QVERIFY(effectsImpl);
    QVERIFY(effectsImpl->loadEffect(effectName));
    QCOMPARE(effectsImpl->loadedEffects().count(), 1);
    QCOMPARE(effectsImpl->loadedEffects().constFirst(), effectName);
    Effect* effect = effectsImpl->findEffect(effectName);
    QVERIFY(effect);
    QVERIFY(!effect->isActive());

    // Switch to the second virtual desktop.
    vd_manager->setCurrent(2u);
    QCOMPARE(vd_manager->current(), 2u);
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
