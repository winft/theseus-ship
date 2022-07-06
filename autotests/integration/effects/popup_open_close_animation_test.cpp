/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "lib/app.h"

#include "base/wayland/server.h"
#include "render/effect_loader.h"
#include "render/effects.h"
#include "toplevel.h"
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

namespace KWin
{

class PopupOpenCloseAnimationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testAnimatePopups();
    void testAnimateUserActionsPopup();
    void testAnimateDecorationTooltips();
};

void PopupOpenCloseAnimationTest::initTestCase()
{
    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());
    qRegisterMetaType<win::internal_window*>();
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    const auto builtinNames = render::effect_loader(*Test::app()->workspace).listOfKnownEffects();
    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }
    config->sync();
    kwinApp()->setConfig(config);

    qputenv("KWIN_EFFECTS_FORCE_ANIMATIONS", QByteArrayLiteral("1"));

    Test::app()->start();
    QVERIFY(startup_spy.size() || startup_spy.wait());
}

void PopupOpenCloseAnimationTest::init()
{
    Test::setup_wayland_connection(Test::global_selection::xdg_decoration);
}

void PopupOpenCloseAnimationTest::cleanup()
{
    auto effectsImpl = qobject_cast<render::effects_handler_impl*>(effects);
    QVERIFY(effectsImpl);
    effectsImpl->unloadAllEffects();
    QVERIFY(effectsImpl->loadedEffects().isEmpty());

    Test::destroy_wayland_connection();
}

void PopupOpenCloseAnimationTest::testAnimatePopups()
{
    // This test verifies that popup open/close animation effects try
    // to animate popups(e.g. popup menus, tooltips, etc).

    // Make sure that we have the right effects ptr.
    auto effectsImpl = qobject_cast<render::effects_handler_impl*>(effects);
    QVERIFY(effectsImpl);

    // Create the main window.
    using namespace Wrapland::Client;
    std::unique_ptr<Surface> mainWindowSurface(Test::create_surface());
    QVERIFY(mainWindowSurface);
    auto mainWindowShellSurface = Test::create_xdg_shell_toplevel(mainWindowSurface);
    QVERIFY(mainWindowShellSurface);
    auto mainWindow = Test::render_and_wait_for_shown(mainWindowSurface, QSize(100, 50), Qt::blue);
    QVERIFY(mainWindow);

    // Load effect that will be tested.
    const QString effectName = QStringLiteral("kwin4_effect_fadingpopups");
    QVERIFY(effectsImpl->loadEffect(effectName));
    QCOMPARE(effectsImpl->loadedEffects().count(), 1);
    QCOMPARE(effectsImpl->loadedEffects().first(), effectName);
    Effect* effect = effectsImpl->findEffect(effectName);
    QVERIFY(effect);
    QVERIFY(!effect->isActive());

    // Create a popup, it should be animated.
    std::unique_ptr<Surface> popupSurface(Test::create_surface());
    QVERIFY(popupSurface);
    XdgPositioner positioner(QSize(20, 20), QRect(0, 0, 10, 10));
    positioner.setGravity(Qt::BottomEdge | Qt::RightEdge);
    positioner.setAnchorEdge(Qt::BottomEdge | Qt::LeftEdge);
    auto popupShellSurface
        = Test::create_xdg_shell_popup(popupSurface, mainWindowShellSurface, positioner);
    QVERIFY(popupShellSurface);
    auto popup = Test::render_and_wait_for_shown(popupSurface, positioner.initialSize(), Qt::red);
    QVERIFY(popup);
    QVERIFY(win::is_popup(popup));
    QCOMPARE(popup->transient()->lead(), mainWindow);
    QVERIFY(effect->isActive());

    // Eventually, the animation will be complete.
    QTRY_VERIFY(!effect->isActive());

    // Destroy the popup, it should not be animated.
    QSignalSpy popupClosedSpy(popup, &win::wayland::window::closed);
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

void PopupOpenCloseAnimationTest::testAnimateUserActionsPopup()
{
    // This test verifies that popup open/close animation effects try
    // to animate the user actions popup.

    // Make sure that we have the right effects ptr.
    auto effectsImpl = qobject_cast<render::effects_handler_impl*>(effects);
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
    QCOMPARE(effectsImpl->loadedEffects().first(), effectName);
    Effect* effect = effectsImpl->findEffect(effectName);
    QVERIFY(effect);
    QVERIFY(!effect->isActive());

    // Show the user actions popup.
    Test::app()->workspace->user_actions_menu->show(QRect(), client);
    auto& userActionsMenu = Test::app()->workspace->user_actions_menu;
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

void PopupOpenCloseAnimationTest::testAnimateDecorationTooltips()
{
    // This test verifies that popup open/close animation effects try
    // to animate decoration tooltips.

    // Make sure that we have the right effects ptr.
    auto effectsImpl = qobject_cast<render::effects_handler_impl*>(effects);
    QVERIFY(effectsImpl);

    // Create the test client.
    using namespace Wrapland::Client;
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);
    std::unique_ptr<XdgDecoration> deco(
        Test::get_client().interfaces.xdg_decoration->getToplevelDecoration(shellSurface.get()));
    QVERIFY(deco);
    deco->setMode(XdgDecoration::Mode::ServerSide);
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QVERIFY(win::decoration(client));

    // Load effect that will be tested.
    const QString effectName = QStringLiteral("kwin4_effect_fadingpopups");
    QVERIFY(effectsImpl->loadEffect(effectName));
    QCOMPARE(effectsImpl->loadedEffects().count(), 1);
    QCOMPARE(effectsImpl->loadedEffects().first(), effectName);
    Effect* effect = effectsImpl->findEffect(effectName);
    QVERIFY(effect);
    QVERIFY(!effect->isActive());

    // Show a decoration tooltip.
    QSignalSpy tooltipAddedSpy(Test::app()->workspace->qobject.get(),
                               &win::space::qobject_t::internalClientAdded);
    QVERIFY(tooltipAddedSpy.isValid());
    client->control->deco().client->requestShowToolTip(QStringLiteral("KWin rocks!"));
    QVERIFY(tooltipAddedSpy.wait());
    auto tooltip = tooltipAddedSpy.first().first().value<win::internal_window*>();
    QVERIFY(tooltip->isInternal());
    QVERIFY(win::is_popup(tooltip));
    QVERIFY(tooltip->internalWindow()->flags().testFlag(Qt::ToolTip));
    QVERIFY(effect->isActive());

    // Eventually, the animation will be complete.
    QTRY_VERIFY(!effect->isActive());

    // Hide the decoration tooltip.
    QSignalSpy tooltipClosedSpy(tooltip, &win::internal_window::closed);
    QVERIFY(tooltipClosedSpy.isValid());
    client->control->deco().client->requestHideToolTip();
    QVERIFY(tooltipClosedSpy.wait());
    QVERIFY(effect->isActive());

    // Eventually, the animation will be complete.
    QTRY_VERIFY(!effect->isActive());

    // Destroy the test client.
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

}

WAYLANDTEST_MAIN(KWin::PopupOpenCloseAnimationTest)
#include "popup_open_close_animation_test.moc"
