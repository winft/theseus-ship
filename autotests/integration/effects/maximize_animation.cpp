/*
SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "render/effect_loader.h"
#include "render/effects.h"
#include "render/scene.h"
#include "win/active_window.h"
#include "win/control.h"
#include "win/space.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>
#include <catch2/generators/catch_generators.hpp>

namespace KWin::detail::test
{

TEST_CASE("maximize animation", "[effect]")
{
    qputenv("KWIN_EFFECTS_FORCE_ANIMATIONS", QByteArrayLiteral("1"));
    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());

    auto operation_mode = GENERATE(base::operation_mode::wayland, base::operation_mode::xwayland);
    test::setup setup("maximize-animation", operation_mode);
    auto config = setup.base->config.main;
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    auto const builtinNames
        = render::effect_loader(*effects, *setup.base->render->compositor).listOfKnownEffects();

    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }

    config->sync();

    setup.start();
    setup_wayland_connection();

    SECTION("maximize restore")
    {
        // This test verifies that the maximize effect animates a client
        // when it's maximized or restored.

        using namespace Wrapland::Client;

        // Create the test client.
        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);

        std::unique_ptr<XdgShellToplevel> shellSurface(
            create_xdg_shell_toplevel(surface, CreationSetup::CreateOnly));

        // Wait for the initial configure event.
        QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configured);

        surface->commit(Surface::CommitFlag::None);

        QVERIFY(configureRequestedSpy.isValid());
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 1);

        auto cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(0, 0));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::maximized));

        // Draw contents of the surface.
        shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());
        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);
        QVERIFY(client->control->active);
        QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);

        // We should receive a configure event when the client becomes active.
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 2);

        cfgdata = shellSurface->get_configure_data();
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::maximized));

        // Load effect that will be tested.
        const QString effectName = QStringLiteral("maximize");
        auto& effectsImpl = setup.base->render->compositor->effects;
        QVERIFY(effectsImpl);
        QVERIFY(effectsImpl->loadEffect(effectName));
        QCOMPARE(effectsImpl->loadedEffects().count(), 1);
        QCOMPARE(effectsImpl->loadedEffects().constFirst(), effectName);
        Effect* effect = effectsImpl->findEffect(effectName);
        QVERIFY(effect);
        QVERIFY(!effect->isActive());

        // Maximize the client.
        QSignalSpy geometryChangedSpy(client->qobject.get(),
                                      &win::window_qobject::frame_geometry_changed);
        QVERIFY(geometryChangedSpy.isValid());
        QSignalSpy maximizeChangedSpy(client->qobject.get(),
                                      &win::window_qobject::maximize_mode_changed);
        QVERIFY(maximizeChangedSpy.isValid());

        win::active_window_maximize(*setup.base->space);
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 3);

        cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(1280, 1024));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::maximized));

        // Draw contents of the maximized client.
        shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());
        render(surface, QSize(1280, 1024), Qt::red);
        QVERIFY(geometryChangedSpy.wait());
        QCOMPARE(geometryChangedSpy.count(), 1);
        QCOMPARE(maximizeChangedSpy.count(), 1);
        QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
        QVERIFY(effect->isActive());

        // Eventually, the animation will be complete.
        QTRY_VERIFY(!effect->isActive());

        // Restore the client.
        win::active_window_maximize(*setup.base->space);
        QVERIFY(configureRequestedSpy.wait());
        QCOMPARE(configureRequestedSpy.count(), 4);

        cfgdata = shellSurface->get_configure_data();
        QCOMPARE(cfgdata.size, QSize(100, 50));
        QVERIFY(cfgdata.states.testFlag(xdg_shell_state::activated));
        QVERIFY(!cfgdata.states.testFlag(xdg_shell_state::maximized));

        // Draw contents of the restored client.
        shellSurface->ackConfigure(configureRequestedSpy.back().front().value<quint32>());
        render(surface, QSize(100, 50), Qt::blue);
        QVERIFY(geometryChangedSpy.wait());
        QCOMPARE(geometryChangedSpy.count(), 2);
        QCOMPARE(maximizeChangedSpy.count(), 2);
        QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
        QVERIFY(effect->isActive());

        // Eventually, the animation will be complete.
        QTRY_VERIFY(!effect->isActive());

        // Destroy the test client.
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }
}

}
