/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

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
#include "render/compositor.h"
#include "render/effect_loader.h"
#include "render/effects.h"
#include "render/scene.h"
#include "toplevel.h"
#include "win/net.h"
#include "win/space.h"
#include "win/transient.h"
#include "win/wayland/window.h"

#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>

namespace KWin
{

class ToplevelOpenCloseAnimationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testAnimateToplevels_data();
    void testAnimateToplevels();
    void testDontAnimatePopups_data();
    void testDontAnimatePopups();
};

void ToplevelOpenCloseAnimationTest::initTestCase()
{
    qputenv("XDG_DATA_DIRS", QCoreApplication::applicationDirPath().toUtf8());
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    const auto builtinNames = render::effect_loader().listOfKnownEffects();
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
    QCOMPARE(scene->compositingType(), KWin::OpenGLCompositing);
}

void ToplevelOpenCloseAnimationTest::init()
{
    Test::setup_wayland_connection();
}

void ToplevelOpenCloseAnimationTest::cleanup()
{
    auto effectsImpl = qobject_cast<render::effects_handler_impl*>(effects);
    QVERIFY(effectsImpl);
    effectsImpl->unloadAllEffects();
    QVERIFY(effectsImpl->loadedEffects().isEmpty());

    Test::destroy_wayland_connection();
}

void ToplevelOpenCloseAnimationTest::testAnimateToplevels_data()
{
    QTest::addColumn<QString>("effectName");

    QTest::newRow("Fade") << QStringLiteral("kwin4_effect_fade");
    QTest::newRow("Glide") << QStringLiteral("glide");
    QTest::newRow("Scale") << QStringLiteral("kwin4_effect_scale");
}

void ToplevelOpenCloseAnimationTest::testAnimateToplevels()
{
    // This test verifies that window open/close animation effects try to
    // animate the appearing and the disappearing of toplevel windows.

    // Make sure that we have the right effects ptr.
    auto effectsImpl = qobject_cast<render::effects_handler_impl*>(effects);
    QVERIFY(effectsImpl);

    // Load effect that will be tested.
    QFETCH(QString, effectName);
    QVERIFY(effectsImpl->loadEffect(effectName));
    QCOMPARE(effectsImpl->loadedEffects().count(), 1);
    QCOMPARE(effectsImpl->loadedEffects().first(), effectName);
    Effect* effect = effectsImpl->findEffect(effectName);
    QVERIFY(effect);
    QVERIFY(!effect->isActive());

    // Create the test client.
    using namespace Wrapland::Client;
    std::unique_ptr<Surface> surface(Test::create_surface());
    QVERIFY(surface);
    std::unique_ptr<XdgShellToplevel> shellSurface(Test::create_xdg_shell_toplevel(surface));
    QVERIFY(shellSurface);
    auto client = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(client);
    QVERIFY(effect->isActive());

    // Eventually, the animation will be complete.
    QTRY_VERIFY(!effect->isActive());

    // Close the test client, the effect should start animating the disappearing
    // of the client.
    QSignalSpy windowClosedSpy(client, &win::wayland::window::windowClosed);
    QVERIFY(windowClosedSpy.isValid());
    shellSurface.reset();
    surface.reset();
    QVERIFY(windowClosedSpy.wait());
    QVERIFY(effect->isActive());

    // Eventually, the animation will be complete.
    QTRY_VERIFY(!effect->isActive());
}

void ToplevelOpenCloseAnimationTest::testDontAnimatePopups_data()
{
    QTest::addColumn<QString>("effectName");

    QTest::newRow("Fade") << QStringLiteral("kwin4_effect_fade");
    QTest::newRow("Glide") << QStringLiteral("glide");
    QTest::newRow("Scale") << QStringLiteral("kwin4_effect_scale");
}

void ToplevelOpenCloseAnimationTest::testDontAnimatePopups()
{
    // This test verifies that window open/close animation effects don't try
    // to animate popups(e.g. popup menus, tooltips, etc).

    // Make sure that we have the right effects ptr.
    auto effectsImpl = qobject_cast<render::effects_handler_impl*>(effects);
    QVERIFY(effectsImpl);

    // Create the main window.
    using namespace Wrapland::Client;
    std::unique_ptr<Surface> mainWindowSurface(Test::create_surface());
    QVERIFY(mainWindowSurface);
    std::unique_ptr<XdgShellToplevel> mainWindowShellSurface(
        Test::create_xdg_shell_toplevel(mainWindowSurface));
    QVERIFY(mainWindowShellSurface);
    auto mainWindow = Test::render_and_wait_for_shown(mainWindowSurface, QSize(100, 50), Qt::blue);
    QVERIFY(mainWindow);

    // Load effect that will be tested.
    QFETCH(QString, effectName);
    QVERIFY(effectsImpl->loadEffect(effectName));
    QCOMPARE(effectsImpl->loadedEffects().count(), 1);
    QCOMPARE(effectsImpl->loadedEffects().first(), effectName);
    Effect* effect = effectsImpl->findEffect(effectName);
    QVERIFY(effect);
    QVERIFY(!effect->isActive());

    // Create a popup, it should not be animated.
    std::unique_ptr<Surface> popupSurface(Test::create_surface());
    QVERIFY(popupSurface);
    XdgPositioner positioner(QSize(20, 20), QRect(0, 0, 10, 10));
    positioner.setGravity(Qt::BottomEdge | Qt::RightEdge);
    positioner.setAnchorEdge(Qt::BottomEdge | Qt::LeftEdge);
    std::unique_ptr<XdgShellPopup> popupShellSurface(
        Test::create_xdg_shell_popup(popupSurface, mainWindowShellSurface, positioner));
    QVERIFY(popupShellSurface);
    auto popup = Test::render_and_wait_for_shown(popupSurface, positioner.initialSize(), Qt::red);
    QVERIFY(popup);
    QVERIFY(win::is_popup(popup));
    QCOMPARE(popup->transient()->lead(), mainWindow);
    QVERIFY(!effect->isActive());

    // Destroy the popup, it should not be animated.
    QSignalSpy popupClosedSpy(popup, &win::wayland::window::windowClosed);
    QVERIFY(popupClosedSpy.isValid());
    popupShellSurface.reset();
    popupSurface.reset();
    QVERIFY(popupClosedSpy.wait());
    QVERIFY(!effect->isActive());

    // Destroy the main window.
    mainWindowSurface.reset();
    QVERIFY(Test::wait_for_destroyed(mainWindow));
}

}

WAYLANDTEST_MAIN(KWin::ToplevelOpenCloseAnimationTest)
#include "toplevel_open_close_animation_test.moc"
