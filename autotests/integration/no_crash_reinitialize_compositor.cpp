/*
SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>
#include <catch2/generators/catch_generators.hpp>

namespace KWin::detail::test
{

TEST_CASE("no crash reinit compositor", "[render]")
{
    // This test verifies that KWin doesn't crash when the compositor settings have been changed
    // while a scripted effect animates the disappearing of a window.

    using namespace Wrapland::Client;

    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());
    qputenv("KWIN_EFFECTS_FORCE_ANIMATIONS", QByteArrayLiteral("1"));
    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));

    test::setup setup("no-crash-reinit-compositor");

    auto config = setup.base->config.main;
    KConfigGroup plugins(config, QStringLiteral("Plugins"));

    auto const builtinNames = render::effect_loader(*setup.base->mod.render).listOfKnownEffects();
    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }

    config->sync();

    setup.start();
    setup.set_outputs(2);
    test_outputs_default();

    auto& scene = setup.base->mod.render->scene;
    QVERIFY(scene);
    REQUIRE(scene->isOpenGl());

    auto effect_name = GENERATE(as<QString>{}, "fade", "glide", "scale");

    // Make sure that we have the right effects ptr.
    auto& effectsImpl = setup.base->mod.render->effects;
    QVERIFY(effectsImpl);

    // Create the test client.
    setup_wayland_connection();

    std::unique_ptr<Surface> surface(create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);

    // Make sure that only the test effect is loaded.
    QVERIFY(effectsImpl->loadEffect(effect_name));
    QCOMPARE(effectsImpl->loadedEffects().count(), 1);
    QCOMPARE(effectsImpl->loadedEffects().constFirst(), effect_name);
    Effect* effect = effectsImpl->findEffect(effect_name);
    QVERIFY(effect);
    QVERIFY(!effect->isActive());

    // Close the test client.
    QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
    QVERIFY(windowClosedSpy.isValid());
    shellSurface.reset();
    surface.reset();
    QVERIFY(windowClosedSpy.wait());

    // The test effect should start animating the test client. Is there a better
    // way to verify that the test effect actually animates the test client?
    QTRY_VERIFY(effect->isActive());

    // Re-initialize the compositor, effects will be destroyed and created again.
    setup.base->mod.render->reinitialize();

    // By this time the compositor should still be alive.
}

}
