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
#include "win/actions.h"
#include "win/net.h"
#include "win/space.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/plasmashell.h>
#include <Wrapland/Client/plasmawindowmanagement.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>
#include <catch2/generators/catch_generators.hpp>

namespace KWin::detail::test
{

TEST_CASE("minimize animation", "[effect]")
{
    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));
    qputenv("KWIN_EFFECTS_FORCE_ANIMATIONS", QByteArrayLiteral("1"));
    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());

    auto operation_mode = GENERATE(base::operation_mode::wayland, base::operation_mode::xwayland);
    test::setup setup("minimize-animation", operation_mode);
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

    setup_wayland_connection(global_selection::plasma_shell | global_selection::window_management);

    SECTION("minimize unminimize")
    {
        // This test verifies that a minimize effect tries to animate a client
        // when it's minimized or unminimized.
        using namespace Wrapland::Client;

        auto effectName = GENERATE(QString("magiclamp"), QString("squash"));

        QSignalSpy plasmaWindowCreatedSpy(get_client().interfaces.window_management.get(),
                                          &PlasmaWindowManagement::windowCreated);
        QVERIFY(plasmaWindowCreatedSpy.isValid());

        // Create a panel at the top of the screen.
        const QRect panelRect = QRect(0, 0, 1280, 36);
        std::unique_ptr<Surface> panelSurface(create_surface());
        QVERIFY(panelSurface);
        std::unique_ptr<XdgShellToplevel> panelShellSurface(
            create_xdg_shell_toplevel(panelSurface));
        QVERIFY(panelShellSurface);
        std::unique_ptr<PlasmaShellSurface> plasmaPanelShellSurface(
            get_client().interfaces.plasma_shell->createSurface(panelSurface.get()));
        QVERIFY(plasmaPanelShellSurface);
        plasmaPanelShellSurface->setRole(PlasmaShellSurface::Role::Panel);
        plasmaPanelShellSurface->setPosition(panelRect.topLeft());
        plasmaPanelShellSurface->setPanelBehavior(PlasmaShellSurface::PanelBehavior::AlwaysVisible);
        auto panel = render_and_wait_for_shown(panelSurface, panelRect.size(), Qt::blue);
        QVERIFY(panel);
        QVERIFY(win::is_dock(panel));
        QCOMPARE(panel->geo.frame, panelRect);
        QVERIFY(plasmaWindowCreatedSpy.wait());
        QCOMPARE(plasmaWindowCreatedSpy.count(), 1);

        // Create the test client.
        std::unique_ptr<Surface> surface(create_surface());
        QVERIFY(surface);
        std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface));
        QVERIFY(shellSurface);
        auto client = render_and_wait_for_shown(surface, QSize(100, 50), Qt::red);
        QVERIFY(client);
        QVERIFY(plasmaWindowCreatedSpy.wait());
        QCOMPARE(plasmaWindowCreatedSpy.count(), 2);

        // We have to set the minimized geometry because the squash effect needs it,
        // otherwise it won't start animation.
        auto window = plasmaWindowCreatedSpy.last().first().value<PlasmaWindow*>();
        QVERIFY(window);
        const QRect iconRect = QRect(0, 0, 42, 36);
        window->setMinimizedGeometry(panelSurface.get(), iconRect);
        flush_wayland_connection();
        QTRY_COMPARE(win::get_icon_geometry(*client),
                     iconRect.translated(panel->geo.frame.topLeft()));

        // Load effect that will be tested.
        auto& effectsImpl = setup.base->render->compositor->effects;
        QVERIFY(effectsImpl);
        QVERIFY(effectsImpl->loadEffect(effectName));
        QCOMPARE(effectsImpl->loadedEffects().count(), 1);
        QCOMPARE(effectsImpl->loadedEffects().constFirst(), effectName);
        Effect* effect = effectsImpl->findEffect(effectName);
        QVERIFY(effect);
        QVERIFY(!effect->isActive());

        // Start the minimize animation.
        win::set_minimized(client, true);
        QVERIFY(effect->isActive());

        // Eventually, the animation will be complete.
        QTRY_VERIFY(!effect->isActive());

        // Start the unminimize animation.
        win::set_minimized(client, false);
        QVERIFY(effect->isActive());

        // Eventually, the animation will be complete.
        QTRY_VERIFY(!effect->isActive());

        // Destroy the panel.
        panelSurface.reset();
        QVERIFY(wait_for_destroyed(panel));

        // Destroy the test client.
        surface.reset();
        QVERIFY(wait_for_destroyed(client));
    }
}

}
