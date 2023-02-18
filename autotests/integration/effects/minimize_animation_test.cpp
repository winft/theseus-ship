/*
SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/app.h"

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

namespace KWin
{

class MinimizeAnimationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testMinimizeUnminimize_data();
    void testMinimizeUnminimize();
};

void MinimizeAnimationTest::initTestCase()
{
    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());

    QSignalSpy startup_spy(Test::app(), &WaylandTestApplication::startup_finished);
    QVERIFY(startup_spy.isValid());

    auto config = Test::app()->base->config.main;
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    auto const builtinNames
        = render::effect_loader(*effects, *Test::app()->base->render->compositor)
              .listOfKnownEffects();

    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }

    config->sync();

    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));
    qputenv("KWIN_EFFECTS_FORCE_ANIMATIONS", QByteArrayLiteral("1"));

    Test::app()->start();
    QVERIFY(startup_spy.size() || startup_spy.wait());

    auto& scene = Test::app()->base->render->compositor->scene;
    QVERIFY(scene);
    QCOMPARE(scene->compositingType(), OpenGLCompositing);
}

void MinimizeAnimationTest::init()
{
    Test::setup_wayland_connection(Test::global_selection::plasma_shell
                                   | Test::global_selection::window_management);
}

void MinimizeAnimationTest::cleanup()
{
    auto& effectsImpl = Test::app()->base->render->compositor->effects;
    QVERIFY(effectsImpl);
    effectsImpl->unloadAllEffects();
    QVERIFY(effectsImpl->loadedEffects().isEmpty());

    Test::destroy_wayland_connection();
}

void MinimizeAnimationTest::testMinimizeUnminimize_data()
{
    QTest::addColumn<QString>("effectName");

    QTest::newRow("Magic Lamp") << QStringLiteral("magiclamp");
    QTest::newRow("Squash") << QStringLiteral("kwin4_effect_squash");
}

void MinimizeAnimationTest::testMinimizeUnminimize()
{
    // This test verifies that a minimize effect tries to animate a client
    // when it's minimized or unminimized.

    using namespace Wrapland::Client;

    QSignalSpy plasmaWindowCreatedSpy(Test::get_client().interfaces.window_management.get(),
                                      &PlasmaWindowManagement::windowCreated);
    QVERIFY(plasmaWindowCreatedSpy.isValid());

    // Create a panel at the top of the screen.
    const QRect panelRect = QRect(0, 0, 1280, 36);
    std::unique_ptr<Surface> panelSurface(Test::create_surface());
    QVERIFY(panelSurface);
    std::unique_ptr<XdgShellToplevel> panelShellSurface(
        Test::create_xdg_shell_toplevel(panelSurface));
    QVERIFY(panelShellSurface);
    std::unique_ptr<PlasmaShellSurface> plasmaPanelShellSurface(
        Test::get_client().interfaces.plasma_shell->createSurface(panelSurface.get()));
    QVERIFY(plasmaPanelShellSurface);
    plasmaPanelShellSurface->setRole(PlasmaShellSurface::Role::Panel);
    plasmaPanelShellSurface->setPosition(panelRect.topLeft());
    plasmaPanelShellSurface->setPanelBehavior(PlasmaShellSurface::PanelBehavior::AlwaysVisible);
    auto panel = Test::render_and_wait_for_shown(panelSurface, panelRect.size(), Qt::blue);
    QVERIFY(panel);
    QVERIFY(win::is_dock(panel));
    QCOMPARE(panel->geo.frame, panelRect);
    QVERIFY(plasmaWindowCreatedSpy.wait());
    QCOMPARE(plasmaWindowCreatedSpy.count(), 1);

    // Create the test client.
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::red);
    QVERIFY(client);
    QVERIFY(plasmaWindowCreatedSpy.wait());
    QCOMPARE(plasmaWindowCreatedSpy.count(), 2);

    // We have to set the minimized geometry because the squash effect needs it,
    // otherwise it won't start animation.
    auto window = plasmaWindowCreatedSpy.last().first().value<PlasmaWindow*>();
    QVERIFY(window);
    const QRect iconRect = QRect(0, 0, 42, 36);
    window->setMinimizedGeometry(panelSurface.get(), iconRect);
    Test::flush_wayland_connection();
    QTRY_COMPARE(win::get_icon_geometry(*client), iconRect.translated(panel->geo.frame.topLeft()));

    // Load effect that will be tested.
    QFETCH(QString, effectName);
    auto& effectsImpl = Test::app()->base->render->compositor->effects;
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
    QVERIFY(Test::wait_for_destroyed(panel));

    // Destroy the test client.
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

}

WAYLANDTEST_MAIN(KWin::MinimizeAnimationTest)
#include "minimize_animation_test.moc"
