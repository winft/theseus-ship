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

#include "platform.h"
#include "render/compositor.h"
#include "render/effect_loader.h"
#include "render/effects.h"
#include "render/scene.h"
#include "toplevel.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/wayland/window.h"

#include "effect_builtins.h"

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
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());
    kwinApp()->platform->setInitialWindowSize(QSize(1280, 1024));

    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    render::scripted_effect_loader loader;
    const auto builtinNames = BuiltInEffects::availableEffectNames() << loader.listOfKnownEffects();
    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }
    config->sync();
    kwinApp()->setConfig(config);

    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));
    qputenv("KWIN_EFFECTS_FORCE_ANIMATIONS", QByteArrayLiteral("1"));

    Test::app()->start();
    QVERIFY(startup_spy.size() || startup_spy.wait());

    auto scene = render::compositor::self()->scene();
    QVERIFY(scene);
    QCOMPARE(scene->compositingType(), OpenGLCompositing);
}

void DesktopSwitchingAnimationTest::init()
{
    Test::setup_wayland_connection();
}

void DesktopSwitchingAnimationTest::cleanup()
{
    auto effectsImpl = qobject_cast<render::effects_handler_impl*>(effects);
    QVERIFY(effectsImpl);
    effectsImpl->unloadAllEffects();
    QVERIFY(effectsImpl->loadedEffects().isEmpty());

    VirtualDesktopManager::self()->setCount(1);

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
    VirtualDesktopManager::self()->setCount(2);
    QCOMPARE(VirtualDesktopManager::self()->current(), 1u);
    QCOMPARE(VirtualDesktopManager::self()->count(), 2u);

    // The Fade Desktop effect will do nothing if there are no clients to fade,
    // so we have to create a dummy test client.
    using namespace Wrapland::Client;
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QCOMPARE(client->desktops().count(), 1);
    QCOMPARE(client->desktops().first(), VirtualDesktopManager::self()->desktops().first());

    // Load effect that will be tested.
    QFETCH(QString, effectName);
    auto effectsImpl = qobject_cast<render::effects_handler_impl*>(effects);
    QVERIFY(effectsImpl);
    QVERIFY(effectsImpl->loadEffect(effectName));
    QCOMPARE(effectsImpl->loadedEffects().count(), 1);
    QCOMPARE(effectsImpl->loadedEffects().first(), effectName);
    Effect* effect = effectsImpl->findEffect(effectName);
    QVERIFY(effect);
    QVERIFY(!effect->isActive());

    // Switch to the second virtual desktop.
    VirtualDesktopManager::self()->setCurrent(2u);
    QCOMPARE(VirtualDesktopManager::self()->current(), 2u);
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
