/*
SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "render/compositor.h"
#include "render/effect_loader.h"
#include "render/effects.h"
#include "render/scene.h"
#include "win/net.h"
#include "win/space.h"
#include "win/transient.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>
#include <catch2/generators/catch_generators.hpp>

namespace KWin::detail::test
{

TEST_CASE("window open close animation", "[effect]")
{
    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));
    qputenv("KWIN_EFFECTS_FORCE_ANIMATIONS", QByteArrayLiteral("1"));
    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());

    test::setup setup("window-open-close-animation");

    auto config = setup.base->config.main;
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    auto const builtinNames
        = render::effect_loader(*effects, *setup.base->render->compositor).listOfKnownEffects();
    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }
    config->sync();

    setup.start();

    auto& scene = setup.base->render->compositor->scene;
    QVERIFY(scene);
    QCOMPARE(scene->compositingType(), KWin::OpenGLCompositing);

    setup_wayland_connection();

    SECTION("animate toplevels")
    {
        // This test verifies that window open/close animation effects try to
        // animate the appearing and the disappearing of toplevel windows.
        auto effectName = GENERATE(
            QString("kwin4_effect_fade"), QString("glide"), QString("kwin4_effect_scale"));

        // Make sure that we have the right effects ptr.
        auto& effectsImpl = setup.base->render->compositor->effects;
        QVERIFY(effectsImpl);

        // Load effect that will be tested.
        QVERIFY(effectsImpl->loadEffect(effectName));
        QCOMPARE(effectsImpl->loadedEffects().count(), 1);
        QCOMPARE(effectsImpl->loadedEffects().constFirst(), effectName);
        Effect* effect = effectsImpl->findEffect(effectName);
        QVERIFY(effect);
        QVERIFY(!effect->isActive());

        // Create the test client.
        using namespace Wrapland::Client;
        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);
        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);
        QVERIFY(effect->isActive());

        // Eventually, the animation will be complete.
        QTRY_VERIFY(!effect->isActive());

        // Close the test client, the effect should start animating the disappearing
        // of the client.
        QSignalSpy windowClosedSpy(client->qobject.get(), &win::window_qobject::closed);
        QVERIFY(windowClosedSpy.isValid());
        shellSurface.reset();
        surface.reset();
        QVERIFY(windowClosedSpy.wait());
        QVERIFY(effect->isActive());

        // Eventually, the animation will be complete.
        QTRY_VERIFY(!effect->isActive());
    }

    SECTION("no animate popups")
    {
        // This test verifies that window open/close animation effects don't try
        // to animate popups(e.g. popup menus, tooltips, etc).
        auto effectName = GENERATE(
            QString("kwin4_effect_fade"), QString("glide"), QString("kwin4_effect_scale"));

        // Make sure that we have the right effects ptr.
        auto& effectsImpl = setup.base->render->compositor->effects;
        QVERIFY(effectsImpl);

        // Create the main window.
        using namespace Wrapland::Client;
        std::unique_ptr<Surface> mainWindowSurface(create_surface());
        QVERIFY(mainWindowSurface);
        std::unique_ptr<XdgShellToplevel> mainWindowShellSurface(
            create_xdg_shell_toplevel(mainWindowSurface));
        QVERIFY(mainWindowShellSurface);
        auto mainWindow = render_and_wait_for_shown(mainWindowSurface, QSize(100, 50), Qt::blue);
        QVERIFY(mainWindow);

        // Load effect that will be tested.
        QVERIFY(effectsImpl->loadEffect(effectName));
        QCOMPARE(effectsImpl->loadedEffects().count(), 1);
        QCOMPARE(effectsImpl->loadedEffects().constFirst(), effectName);

        auto effect = effectsImpl->findEffect(effectName);
        QVERIFY(effect);
        QVERIFY(!effect->isActive());

        // Create a popup, it should not be animated.
        std::unique_ptr<Surface> popupSurface(create_surface());
        QVERIFY(popupSurface);

        Wrapland::Client::xdg_shell_positioner_data pos_data;
        pos_data.size = QSize(20, 20);
        pos_data.anchor.rect = QRect(0, 0, 10, 10);
        pos_data.anchor.edge = Qt::BottomEdge | Qt::LeftEdge;
        pos_data.gravity = Qt::BottomEdge | Qt::RightEdge;

        std::unique_ptr<XdgShellPopup> popupShellSurface(
            create_xdg_shell_popup(popupSurface, mainWindowShellSurface, pos_data));
        QVERIFY(popupShellSurface);
        auto popup = render_and_wait_for_shown(popupSurface, pos_data.size, Qt::red);
        QVERIFY(popup);
        QVERIFY(win::is_popup(popup));
        QCOMPARE(popup->transient->lead(), mainWindow);
        QVERIFY(!effect->isActive());

        // Destroy the popup, it should not be animated.
        QSignalSpy popupClosedSpy(popup->qobject.get(), &win::window_qobject::closed);
        QVERIFY(popupClosedSpy.isValid());
        popupShellSurface.reset();
        popupSurface.reset();
        QVERIFY(popupClosedSpy.wait());
        QVERIFY(!effect->isActive());

        // Destroy the main window.
        mainWindowSurface.reset();
        QVERIFY(wait_for_destroyed(mainWindow));
    }
}

}
