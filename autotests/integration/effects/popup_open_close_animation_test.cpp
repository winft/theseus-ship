/*
SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/setup.h"

#include "base/wayland/server.h"
#include "render/effect_loader.h"
#include "render/effects.h"
#include "win/deco.h"
#include "win/deco/client_impl.h"
#include "win/internal_window.h"
#include "win/net.h"
#include "win/space.h"
#include "win/transient.h"
#include "win/user_actions_menu.h"

#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>
#include <Wrapland/Client/xdgdecoration.h>

#include <linux/input.h>

namespace KWin::detail::test
{

TEST_CASE("popup open close animation", "[effect]")
{
    qputenv("KWIN_EFFECTS_FORCE_ANIMATIONS", QByteArrayLiteral("1"));
    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());

    test::setup setup("popup-open-close-animation");

    auto config = setup.base->config.main;
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    auto const builtinNames
        = render::effect_loader(*effects, *setup.base->render->compositor).listOfKnownEffects();

    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }

    config->sync();

    setup.start();
    Test::setup_wayland_connection(Test::global_selection::xdg_decoration);

    SECTION("animate popups")
    {
        // This test verifies that popup open/close animation effects try
        // to animate popups(e.g. popup menus, tooltips, etc).

        // Make sure that we have the right effects ptr.
        auto& effectsImpl = setup.base->render->compositor->effects;
        QVERIFY(effectsImpl);

        // Create the main window.
        using namespace Wrapland::Client;
        std::unique_ptr<Surface> mainWindowSurface(Test::create_surface());
        QVERIFY(mainWindowSurface);
        auto mainWindowShellSurface = Test::create_xdg_shell_toplevel(mainWindowSurface);
        QVERIFY(mainWindowShellSurface);
        auto mainWindow
            = Test::render_and_wait_for_shown(mainWindowSurface, QSize(100, 50), Qt::blue);
        QVERIFY(mainWindow);

        // Load effect that will be tested.
        const QString effectName = QStringLiteral("kwin4_effect_fadingpopups");
        QVERIFY(effectsImpl->loadEffect(effectName));
        QCOMPARE(effectsImpl->loadedEffects().count(), 1);
        QCOMPARE(effectsImpl->loadedEffects().constFirst(), effectName);
        Effect* effect = effectsImpl->findEffect(effectName);
        QVERIFY(effect);
        QVERIFY(!effect->isActive());

        // Create a popup, it should be animated.
        std::unique_ptr<Surface> popupSurface(Test::create_surface());
        QVERIFY(popupSurface);

        xdg_shell_positioner_data pos_data;
        pos_data.size = QSize(20, 20);
        pos_data.anchor.rect = QRect(0, 0, 10, 10);
        pos_data.anchor.edge = Qt::BottomEdge | Qt::LeftEdge;
        pos_data.gravity = Qt::BottomEdge | Qt::RightEdge;

        auto popupShellSurface
            = Test::create_xdg_shell_popup(popupSurface, mainWindowShellSurface, pos_data);
        QVERIFY(popupShellSurface);
        auto popup = Test::render_and_wait_for_shown(popupSurface, pos_data.size, Qt::red);
        QVERIFY(popup);
        QVERIFY(win::is_popup(popup));
        QCOMPARE(popup->transient->lead(), mainWindow);
        QVERIFY(effect->isActive());

        // Eventually, the animation will be complete.
        QTRY_VERIFY(!effect->isActive());

        // Destroy the popup, it should not be animated.
        QSignalSpy popupClosedSpy(popup->qobject.get(), &win::window_qobject::closed);
        QVERIFY(popupClosedSpy.isValid());
        popupShellSurface.reset();
        popupSurface.reset();
        QVERIFY(popupClosedSpy.wait());
        QVERIFY(effect->isActive());

        // Eventually, the animation will be complete.
        QTRY_VERIFY(!effect->isActive());

        // Destroy the main window.
        mainWindowSurface.reset();
        QVERIFY(Test::wait_for_destroyed(mainWindow));
    }

    SECTION("animate user actions popup")
    {
        // This test verifies that popup open/close animation effects try
        // to animate the user actions popup.

        // Make sure that we have the right effects ptr.
        auto& effectsImpl = setup.base->render->compositor->effects;
        QVERIFY(effectsImpl);

        // Create the test client.
        using namespace Wrapland::Client;
        std::unique_ptr<Surface> surface(Test::create_surface());
        QVERIFY(surface);
        auto shellSurface = Test::create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);
        auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);

        // Load effect that will be tested.
        const QString effectName = QStringLiteral("kwin4_effect_fadingpopups");
        QVERIFY(effectsImpl->loadEffect(effectName));
        QCOMPARE(effectsImpl->loadedEffects().count(), 1);
        QCOMPARE(effectsImpl->loadedEffects().constFirst(), effectName);
        Effect* effect = effectsImpl->findEffect(effectName);
        QVERIFY(effect);
        QVERIFY(!effect->isActive());

        // Show the user actions popup.
        setup.base->space->user_actions_menu->show(QRect(), client);
        auto& userActionsMenu = setup.base->space->user_actions_menu;
        QTRY_VERIFY(userActionsMenu->isShown());
        QVERIFY(userActionsMenu->hasClient());
        QVERIFY(effect->isActive());

        // Eventually, the animation will be complete.
        QTRY_VERIFY(!effect->isActive());

        // Close the user actions popup.
        Test::keyboard_key_pressed(KEY_ESC, 0);
        Test::keyboard_key_released(KEY_ESC, 1);
        QTRY_VERIFY(!userActionsMenu->isShown());
        QVERIFY(!userActionsMenu->hasClient());
        QVERIFY(effect->isActive());

        // Eventually, the animation will be complete.
        QTRY_VERIFY(!effect->isActive());

        // Destroy the test client.
        surface.reset();
        QVERIFY(Test::wait_for_destroyed(client));
    }

    SECTION("animate decoration tooltips")
    {
        // This test verifies that popup open/close animation effects try
        // to animate decoration tooltips.

        // Make sure that we have the right effects ptr.
        auto& effectsImpl = setup.base->render->compositor->effects;
        QVERIFY(effectsImpl);

        // Create the test client.
        using namespace Wrapland::Client;
        std::unique_ptr<Surface> surface(Test::create_surface());
        QVERIFY(surface);
        auto shellSurface = Test::create_xdg_shell_toplevel(surface);
        QVERIFY(shellSurface);
        std::unique_ptr<XdgDecoration> deco(
            Test::get_client().interfaces.xdg_decoration->getToplevelDecoration(
                shellSurface.get()));
        QVERIFY(deco);
        deco->setMode(XdgDecoration::Mode::ServerSide);
        auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
        QVERIFY(client);
        QVERIFY(win::decoration(client));

        // Load effect that will be tested.
        const QString effectName = QStringLiteral("kwin4_effect_fadingpopups");
        QVERIFY(effectsImpl->loadEffect(effectName));
        QCOMPARE(effectsImpl->loadedEffects().count(), 1);
        QCOMPARE(effectsImpl->loadedEffects().constFirst(), effectName);
        Effect* effect = effectsImpl->findEffect(effectName);
        QVERIFY(effect);
        QVERIFY(!effect->isActive());

        // Show a decoration tooltip.
        QSignalSpy tooltipAddedSpy(setup.base->space->qobject.get(),
                                   &win::space_qobject::internalClientAdded);
        QVERIFY(tooltipAddedSpy.isValid());
        client->control->deco.client->requestShowToolTip(QStringLiteral("KWin rocks!"));
        QVERIFY(tooltipAddedSpy.wait());

        auto tooltip_id = tooltipAddedSpy.first().first().value<quint32>();
        auto tooltip = Test::get_internal_window(setup.base->space->windows_map.at(tooltip_id));
        QVERIFY(tooltip);
        QVERIFY(tooltip->isInternal());
        QVERIFY(win::is_popup(tooltip));
        QVERIFY(tooltip->internalWindow()->flags().testFlag(Qt::ToolTip));
        QVERIFY(effect->isActive());

        // Eventually, the animation will be complete.
        QTRY_VERIFY(!effect->isActive());

        // Hide the decoration tooltip.
        QSignalSpy tooltipClosedSpy(tooltip->qobject.get(), &win::window_qobject::closed);
        QVERIFY(tooltipClosedSpy.isValid());
        client->control->deco.client->requestHideToolTip();
        QVERIFY(tooltipClosedSpy.wait());
        QVERIFY(effect->isActive());

        // Eventually, the animation will be complete.
        QTRY_VERIFY(!effect->isActive());

        // Destroy the test client.
        surface.reset();
        QVERIFY(Test::wait_for_destroyed(client));
    }
}

}
