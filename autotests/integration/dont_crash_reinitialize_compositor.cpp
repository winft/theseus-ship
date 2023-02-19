/*
SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "lib/app.h"

#include "base/wayland/server.h"
#include "render/compositor.h"
#include "render/effect_loader.h"
#include "render/effects.h"
#include "win/space.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>

namespace KWin
{

class DontCrashReinitializeCompositorTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testReinitializeCompositor_data();
    void testReinitializeCompositor();
};

void DontCrashReinitializeCompositorTest::initTestCase()
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
    Test::app()->set_outputs(2);

    QVERIFY(startup_spy.count() || startup_spy.wait());
    Test::test_outputs_default();

    auto& scene = Test::app()->base->render->compositor->scene;
    QVERIFY(scene);
    QCOMPARE(scene->compositingType(), KWin::OpenGLCompositing);
}

void DontCrashReinitializeCompositorTest::init()
{
    Test::setup_wayland_connection();
}

void DontCrashReinitializeCompositorTest::cleanup()
{
    // Unload all effects.
    auto& effectsImpl = Test::app()->base->render->compositor->effects;
    QVERIFY(effectsImpl);
    effectsImpl->unloadAllEffects();
    QVERIFY(effectsImpl->loadedEffects().isEmpty());

    Test::destroy_wayland_connection();
}

void DontCrashReinitializeCompositorTest::testReinitializeCompositor_data()
{
    QTest::addColumn<QString>("effectName");

    QTest::newRow("Fade") << QStringLiteral("kwin4_effect_fade");
    QTest::newRow("Glide") << QStringLiteral("glide");
    QTest::newRow("Scale") << QStringLiteral("kwin4_effect_scale");
}

void DontCrashReinitializeCompositorTest::testReinitializeCompositor()
{
    // This test verifies that KWin doesn't crash when the compositor settings
    // have been changed while a scripted effect animates the disappearing of
    // a window.

    // Make sure that we have the right effects ptr.
    auto& effectsImpl = Test::app()->base->render->compositor->effects;
    QVERIFY(effectsImpl);

    // Create the test client.
    using namespace Wrapland::Client;

    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);

    // Make sure that only the test effect is loaded.
    QFETCH(QString, effectName);
    QVERIFY(effectsImpl->loadEffect(effectName));
    QCOMPARE(effectsImpl->loadedEffects().count(), 1);
    QCOMPARE(effectsImpl->loadedEffects().constFirst(), effectName);
    Effect* effect = effectsImpl->findEffect(effectName);
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
    Test::app()->base->render->compositor->reinitialize();

    // By this time, KWin should still be alive.
}

}

WAYLANDTEST_MAIN(KWin::DontCrashReinitializeCompositorTest)
#include "dont_crash_reinitialize_compositor.moc"
