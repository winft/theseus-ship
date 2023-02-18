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
#include "win/space.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>

namespace KWin
{

class DesktopSwitchingAnimationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testSwitchDesktops_data();
    void testSwitchDesktops();
};

void DesktopSwitchingAnimationTest::initTestCase()
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

void DesktopSwitchingAnimationTest::init()
{
    Test::setup_wayland_connection();
}

void DesktopSwitchingAnimationTest::cleanup()
{
    auto& effectsImpl = Test::app()->base->render->compositor->effects;
    QVERIFY(effectsImpl);
    effectsImpl->unloadAllEffects();
    QVERIFY(effectsImpl->loadedEffects().isEmpty());

    Test::app()->base->space->virtual_desktop_manager->setCount(1);
    Test::destroy_wayland_connection();
}

void DesktopSwitchingAnimationTest::testSwitchDesktops_data()
{
    QTest::addColumn<QString>("effectName");

    QTest::newRow("Desktop Cube Animation") << QStringLiteral("cubeslide");
    QTest::newRow("Fade Desktop") << QStringLiteral("kwin4_effect_fadedesktop");
    QTest::newRow("Slide") << QStringLiteral("slide");
}

void DesktopSwitchingAnimationTest::testSwitchDesktops()
{
    // This test verifies that virtual desktop switching animation effects actually
    // try to animate switching between desktops.

    // We need at least 2 virtual desktops for the test.
    auto& vd_manager = Test::app()->base->space->virtual_desktop_manager;
    vd_manager->setCount(2);
    QCOMPARE(vd_manager->current(), 1u);
    QCOMPARE(vd_manager->count(), 2u);

    // The Fade Desktop effect will do nothing if there are no clients to fade,
    // so we have to create a dummy test client.
    using namespace Wrapland::Client;
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QCOMPARE(client->topo.desktops.count(), 1);
    QCOMPARE(client->topo.desktops.constFirst(), vd_manager->desktops().first());

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
    QVERIFY(Test::wait_for_destroyed(client));
}

}

WAYLANDTEST_MAIN(KWin::DesktopSwitchingAnimationTest)
#include "desktop_switching_animation_test.moc"
