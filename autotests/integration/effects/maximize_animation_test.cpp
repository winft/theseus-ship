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

#include "kwin_wayland_test.h"

#include "effectloader.h"
#include "effects.h"
#include "platform.h"
#include "scene.h"
#include "toplevel.h"
#include "wayland_server.h"
#include "workspace.h"

#include "win/control.h"
#include "win/wayland/window.h"

#include "effect_builtins.h"

#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>

using namespace KWin;

static const QString s_socketName = QStringLiteral("wayland_test_effects_maximize_animation-0");

class MaximizeAnimationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testMaximizeRestore();
};

void MaximizeAnimationTest::initTestCase()
{
    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());
    qRegisterMetaType<KWin::Toplevel*>();
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy workspaceCreatedSpy(kwinApp(), &Application::workspaceCreated);
    QVERIFY(workspaceCreatedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName.toLocal8Bit()));

    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    ScriptedEffectLoader loader;
    const auto builtinNames = BuiltInEffects::availableEffectNames() << loader.listOfKnownEffects();
    for (const QString &name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }
    config->sync();
    kwinApp()->setConfig(config);

    qputenv("KWIN_EFFECTS_FORCE_ANIMATIONS", QByteArrayLiteral("1"));

    kwinApp()->start();
    QVERIFY(workspaceCreatedSpy.wait());
    waylandServer()->initWorkspace();
}

void MaximizeAnimationTest::init()
{
    Test::setup_wayland_connection();
}

void MaximizeAnimationTest::cleanup()
{
    auto effectsImpl = qobject_cast<EffectsHandlerImpl *>(effects);
    QVERIFY(effectsImpl);
    effectsImpl->unloadAllEffects();
    QVERIFY(effectsImpl->loadedEffects().isEmpty());

    Test::destroy_wayland_connection();
}

void MaximizeAnimationTest::testMaximizeRestore()
{
    // This test verifies that the maximize effect animates a client
    // when it's maximized or restored.

    using namespace Wrapland::Client;

    // Create the test client.
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);

    std::unique_ptr<XdgShellToplevel> shellSurface(create_xdg_shell_toplevel(surface, Test::CreationSetup::CreateOnly));

    // Wait for the initial configure event.
    XdgShellToplevel::States states;
    QSignalSpy configureRequestedSpy(shellSurface.get(), &XdgShellToplevel::configureRequested);

    surface->commit(Surface::CommitFlag::None);

    QVERIFY(configureRequestedSpy.isValid());
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 1);
    QCOMPARE(configureRequestedSpy.last().at(0).value<QSize>(), QSize(0, 0));
    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Maximized));

    // Draw contents of the surface.
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QVERIFY(client->control->active());
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);

    // We should receive a configure event when the client becomes active.
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 2);
    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Maximized));

    // Load effect that will be tested.
    const QString effectName = QStringLiteral("kwin4_effect_maximize");
    auto effectsImpl = qobject_cast<EffectsHandlerImpl *>(effects);
    QVERIFY(effectsImpl);
    QVERIFY(effectsImpl->loadEffect(effectName));
    QCOMPARE(effectsImpl->loadedEffects().count(), 1);
    QCOMPARE(effectsImpl->loadedEffects().first(), effectName);
    Effect *effect = effectsImpl->findEffect(effectName);
    QVERIFY(effect);
    QVERIFY(!effect->isActive());

    // Maximize the client.
    QSignalSpy geometryChangedSpy(client, &win::wayland::window::frame_geometry_changed);
    QVERIFY(geometryChangedSpy.isValid());
    QSignalSpy maximizeChangedSpy(client,
        qOverload<Toplevel*, bool, bool>(&win::wayland::window::clientMaximizedStateChanged));
    QVERIFY(maximizeChangedSpy.isValid());

    workspace()->slotWindowMaximize();
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 3);
    QCOMPARE(configureRequestedSpy.last().at(0).value<QSize>(), QSize(1280, 1024));
    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(states.testFlag(XdgShellToplevel::State::Maximized));

    // Draw contents of the maximized client.
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface, QSize(1280, 1024), Qt::red);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(geometryChangedSpy.count(), 1);
    QCOMPARE(maximizeChangedSpy.count(), 1);
    QCOMPARE(client->maximizeMode(), win::maximize_mode::full);
    QVERIFY(effect->isActive());

    // Eventually, the animation will be complete.
    QTRY_VERIFY(!effect->isActive());

    // Restore the client.
    workspace()->slotWindowMaximize();
    QVERIFY(configureRequestedSpy.wait());
    QCOMPARE(configureRequestedSpy.count(), 4);
    QCOMPARE(configureRequestedSpy.last().at(0).value<QSize>(), QSize(100, 50));
    states = configureRequestedSpy.last().at(1).value<XdgShellToplevel::States>();
    QVERIFY(states.testFlag(XdgShellToplevel::State::Activated));
    QVERIFY(!states.testFlag(XdgShellToplevel::State::Maximized));

    // Draw contents of the restored client.
    shellSurface->ackConfigure(configureRequestedSpy.last().at(2).value<quint32>());
    Test::render(surface, QSize(100, 50), Qt::blue);
    QVERIFY(geometryChangedSpy.wait());
    QCOMPARE(geometryChangedSpy.count(), 2);
    QCOMPARE(maximizeChangedSpy.count(), 2);
    QCOMPARE(client->maximizeMode(), win::maximize_mode::restore);
    QVERIFY(effect->isActive());

    // Eventually, the animation will be complete.
    QTRY_VERIFY(!effect->isActive());

    // Destroy the test client.
    surface.reset();
    QVERIFY(Test::wait_for_destroyed(client));
}

WAYLANDTEST_MAIN(MaximizeAnimationTest)
#include "maximize_animation_test.moc"
