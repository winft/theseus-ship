/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2018 David Edmundson <davidedmundson@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*********************************************************************/
#include "lib/app.h"

#include "base/wayland/server.h"
#include "kwineffects/anidata_p.h"
#include "render/compositor.h"
#include "render/effect_loader.h"
#include "render/effects.h"
#include "scripting/effect.h"
#include "win/actions.h"
#include "win/space.h"
#include "win/virtual_desktops.h"

#include <QJSValue>
#include <QQmlEngine>

#include <KConfigGroup>
#include <KGlobalAccel>

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/registry.h>
#include <Wrapland/Client/slide.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>

using namespace std::chrono_literals;

namespace KWin
{

class ScriptedEffectsTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testEffectsHandler();
    void testEffectsContext();
    void testShortcuts();
    void testAnimations_data();
    void testAnimations();
    void testScreenEdge();
    void testScreenEdgeTouch();
    void testFullScreenEffect_data();
    void testFullScreenEffect();
    void testKeepAlive_data();
    void testKeepAlive();
    void testGrab();
    void testGrabAlreadyGrabbedWindow();
    void testGrabAlreadyGrabbedWindowForced();
    void testUngrab();
    void testRedirect_data();
    void testRedirect();
    void testComplete();

private:
    scripting::effect* loadEffect(const QString& name);
};

class ScriptedEffectWithDebugSpy : public scripting::effect
{
    Q_OBJECT
public:
    ScriptedEffectWithDebugSpy();
    bool load(const QString& name);
    using AnimationEffect::AniMap;
    using AnimationEffect::state;
    Q_INVOKABLE void sendTestResponse(const QString& out); // proxies triggers out from the tests
    QList<QAction*> actions(); // returns any QActions owned by the ScriptEngine
Q_SIGNALS:
    void testOutput(const QString& data);
};

void ScriptedEffectWithDebugSpy::sendTestResponse(const QString& out)
{
    Q_EMIT testOutput(out);
}

QList<QAction*> ScriptedEffectWithDebugSpy::actions()
{
    return findChildren<QAction*>(QString(), Qt::FindDirectChildrenOnly);
}

ScriptedEffectWithDebugSpy::ScriptedEffectWithDebugSpy()
    : scripting::effect(*Test::app()->base.space)
{
}

bool ScriptedEffectWithDebugSpy::load(const QString& name)
{
    auto selfContext = engine()->newQObject(this);
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    const QString path = QFINDTESTDATA("./scripts/" + name + ".js");
    engine()->globalObject().setProperty("sendTestResponse",
                                         selfContext.property("sendTestResponse"));
    if (!init(name, path)) {
        return false;
    }

    // inject our newly created effect to be registered with the
    // render::effects_handler_impl::loaded_effects this is private API so some horrible code is
    // used to find the internal effectloader and register ourselves
    auto c = effects->children();
    for (auto it = c.begin(); it != c.end(); ++it) {
        if (qstrcmp((*it)->metaObject()->className(), "KWin::render::effect_loader") != 0) {
            continue;
        }
        QMetaObject::invokeMethod(
            *it, "effectLoaded", Q_ARG(KWin::Effect*, this), Q_ARG(QString, name));
        break;
    }

    return (static_cast<render::effects_handler_impl*>(effects)->isEffectLoaded(name));
}

void ScriptedEffectsTest::initTestCase()
{
    qRegisterMetaType<KWin::Effect*>();
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy startup_spy(kwinApp(), &Application::startup_finished);
    QVERIFY(startup_spy.isValid());

    // disable all effects - we don't want to have it interact with the rendering
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    KConfigGroup plugins(config, QStringLiteral("Plugins"));

    const auto builtinNames = render::effect_loader(*Test::app()->base.space).listOfKnownEffects();
    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }

    config->sync();
    kwinApp()->setConfig(config);

    qputenv("KWIN_COMPOSE", QByteArrayLiteral("O2"));
    qputenv("KWIN_EFFECTS_FORCE_ANIMATIONS", "1");

    Test::app()->start();
    QVERIFY(startup_spy.wait());
    QVERIFY(Test::app()->base.render->compositor);

    auto& scene = Test::app()->base.render->compositor->scene;
    QVERIFY(scene);
    QCOMPARE(scene->compositingType(), KWin::OpenGLCompositing);

    Test::app()->base.space->virtual_desktop_manager->setCount(2);
}

void ScriptedEffectsTest::init()
{
    Test::setup_wayland_connection();
}

void ScriptedEffectsTest::cleanup()
{
    Test::destroy_wayland_connection();

    auto effectsImpl = static_cast<render::effects_handler_impl*>(effects);
    effectsImpl->unloadAllEffects();
    QVERIFY(effectsImpl->loadedEffects().isEmpty());

    Test::app()->base.space->virtual_desktop_manager->setCurrent(1);
}

void ScriptedEffectsTest::testEffectsHandler()
{
    // this triggers and tests some of the signals in EffectHandler, which is exposed to JS as
    // context property "effects"
    auto* effect = new ScriptedEffectWithDebugSpy; // cleaned up in ::clean
    QSignalSpy effectOutputSpy(effect, &ScriptedEffectWithDebugSpy::testOutput);
    auto waitFor = [&effectOutputSpy, this](const QString& expected) {
        QVERIFY(effectOutputSpy.count() > 0 || effectOutputSpy.wait());
        QCOMPARE(effectOutputSpy.first().first(), expected);
        effectOutputSpy.removeFirst();
    };
    QVERIFY(effect->load("effectsHandler"));

    // trigger windowAdded signal

    // create a window
    using namespace Wrapland::Client;
    auto surface = Test::create_surface();
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);
    shellSurface->setTitle("WindowA");
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(Test::app()->base.space->active_client, c);

    waitFor("windowAdded - WindowA");
    waitFor("stackingOrder - 1 WindowA");

    // windowMinimsed
    win::set_minimized(c, true);
    waitFor("windowMinimized - WindowA");

    win::set_minimized(c, false);
    waitFor("windowUnminimized - WindowA");

    surface.reset();
    waitFor("windowClosed - WindowA");

    // desktop management
    Test::app()->base.space->virtual_desktop_manager->setCurrent(2);
    waitFor("desktopChanged - 1 2");
}

void ScriptedEffectsTest::testEffectsContext()
{
    // this tests misc non-objects exposed to the script engine: animationTime, displaySize, use of
    // external enums

    auto* effect = new ScriptedEffectWithDebugSpy; // cleaned up in ::clean
    QSignalSpy effectOutputSpy(effect, &ScriptedEffectWithDebugSpy::testOutput);
    QVERIFY(effect->load("effectContext"));
    QCOMPARE(effectOutputSpy[0].first(), "1280x1024");
    QCOMPARE(effectOutputSpy[1].first(), "100");
    QCOMPARE(effectOutputSpy[2].first(), "2");
    QCOMPARE(effectOutputSpy[3].first(), "0");
}

void ScriptedEffectsTest::testShortcuts()
{
    // this tests method registerShortcut
    auto* effect = new ScriptedEffectWithDebugSpy; // cleaned up in ::clean
    QSignalSpy effectOutputSpy(effect, &ScriptedEffectWithDebugSpy::testOutput);
    QVERIFY(effect->load("shortcutsTest"));
    QCOMPARE(effect->actions().count(), 1);
    auto action = effect->actions().constFirst();
    QCOMPARE(action->objectName(), "testShortcut");
    QCOMPARE(action->text(), "Test Shortcut");
    QCOMPARE(KGlobalAccel::self()->shortcut(action).first(), QKeySequence("Meta+Shift+Y"));
    action->trigger();
    QCOMPARE(effectOutputSpy[0].first(), "shortcutTriggered");
}

void ScriptedEffectsTest::testAnimations_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<int>("animationCount");

    QTest::newRow("single") << "animationTest" << 1;
    QTest::newRow("multi") << "animationTestMulti" << 2;
}

void ScriptedEffectsTest::testAnimations()
{
    // this tests animate/set/cancel
    // methods take either an int or an array, as forced in the data above
    // also splits animate vs effects.animate(..)

    QFETCH(QString, file);
    QFETCH(int, animationCount);

    auto* effect = new ScriptedEffectWithDebugSpy;
    QSignalSpy effectOutputSpy(effect, &ScriptedEffectWithDebugSpy::testOutput);
    QVERIFY(effect->load(file));

    // animated after window added connect
    using namespace Wrapland::Client;
    auto surface = Test::create_surface();
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);
    shellSurface->setTitle("Window 1");
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(Test::app()->base.space->active_client, c);

    {
        const auto state = effect->state();
        QCOMPARE(state.count(), 1);
        QCOMPARE(state.firstKey(), c->render->effect.get());
        const auto& animationsForWindow = state.first().first;
        QCOMPARE(animationsForWindow.count(), animationCount);
        QCOMPARE(animationsForWindow[0].timeLine.duration(), 100ms);
        QCOMPARE(animationsForWindow[0].to, FPx2(1.4));
        QCOMPARE(animationsForWindow[0].attribute, AnimationEffect::Scale);
        QCOMPARE(animationsForWindow[0].timeLine.easingCurve().type(), QEasingCurve::OutCubic);
        QCOMPARE(animationsForWindow[0].terminationFlags,
                 AnimationEffect::TerminateAtSource | AnimationEffect::TerminateAtTarget);

        if (animationCount == 2) {
            QCOMPARE(animationsForWindow[1].timeLine.duration(), 100ms);
            QCOMPARE(animationsForWindow[1].to, FPx2(0.0));
            QCOMPARE(animationsForWindow[1].attribute, AnimationEffect::Opacity);
            QCOMPARE(animationsForWindow[1].terminationFlags,
                     AnimationEffect::TerminateAtSource | AnimationEffect::TerminateAtTarget);
        }
    }
    QCOMPARE(effectOutputSpy[0].first(), "true");

    // window state changes, scale should be retargetted

    win::set_minimized(c, true);
    {
        const auto state = effect->state();
        QCOMPARE(state.count(), 1);
        const auto& animationsForWindow = state.first().first;
        QCOMPARE(animationsForWindow.count(), animationCount);
        QCOMPARE(animationsForWindow[0].timeLine.duration(), 200ms);
        QCOMPARE(animationsForWindow[0].to, FPx2(1.5));
        QCOMPARE(animationsForWindow[0].attribute, AnimationEffect::Scale);
        QCOMPARE(animationsForWindow[0].terminationFlags,
                 AnimationEffect::TerminateAtSource | AnimationEffect::TerminateAtTarget);
        if (animationCount == 2) {
            QCOMPARE(animationsForWindow[1].timeLine.duration(), 200ms);
            QCOMPARE(animationsForWindow[1].to, FPx2(1.5));
            QCOMPARE(animationsForWindow[1].attribute, AnimationEffect::Opacity);
            QCOMPARE(animationsForWindow[1].terminationFlags,
                     AnimationEffect::TerminateAtSource | AnimationEffect::TerminateAtTarget);
        }
    }
    win::set_minimized(c, false);
    {
        const auto state = effect->state();
        QCOMPARE(state.count(), 0);
    }
}

void ScriptedEffectsTest::testScreenEdge()
{
    // this test checks registerScreenEdge functions
    auto* effect = new ScriptedEffectWithDebugSpy; // cleaned up in ::clean
    QSignalSpy effectOutputSpy(effect, &ScriptedEffectWithDebugSpy::testOutput);
    QVERIFY(effect->load("screenEdgeTest"));
    effect->borderActivated(KWin::ElectricTopRight);
    QCOMPARE(effectOutputSpy.count(), 1);
}

void ScriptedEffectsTest::testScreenEdgeTouch()
{
    // this test checks registerTouchScreenEdge functions
    auto* effect = new ScriptedEffectWithDebugSpy; // cleaned up in ::clean
    QSignalSpy effectOutputSpy(effect, &ScriptedEffectWithDebugSpy::testOutput);
    QVERIFY(effect->load("screenEdgeTouchTest"));
    effect->actions().constFirst()->trigger();
    QCOMPARE(effectOutputSpy.count(), 1);
}

void ScriptedEffectsTest::testFullScreenEffect_data()
{
    QTest::addColumn<QString>("file");

    QTest::newRow("single") << "fullScreenEffectTest";
    QTest::newRow("multi") << "fullScreenEffectTestMulti";
    QTest::newRow("global") << "fullScreenEffectTestGlobal";
}

void ScriptedEffectsTest::testFullScreenEffect()
{
    QFETCH(QString, file);

    auto* effectMain = new ScriptedEffectWithDebugSpy; // cleaned up in ::clean
    QSignalSpy effectOutputSpy(effectMain, &ScriptedEffectWithDebugSpy::testOutput);
    QSignalSpy fullScreenEffectActiveSpy(effects,
                                         &EffectsHandler::hasActiveFullScreenEffectChanged);
    QSignalSpy isActiveFullScreenEffectSpy(effectMain,
                                           &scripting::effect::isActiveFullScreenEffectChanged);

    QVERIFY(effectMain->load(file));

    // load any random effect from another test to confirm fullscreen effect state is correctly
    // shown as being someone else
    auto effectOther = new ScriptedEffectWithDebugSpy();
    QVERIFY(effectOther->load("screenEdgeTouchTest"));
    QSignalSpy isActiveFullScreenEffectSpyOther(
        effectOther, &scripting::effect::isActiveFullScreenEffectChanged);

    using namespace Wrapland::Client;
    auto surface = Test::create_surface();
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);
    shellSurface->setTitle("Window 1");
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(Test::app()->base.space->active_client, c);

    QCOMPARE(effects->hasActiveFullScreenEffect(), false);
    QCOMPARE(effectMain->isActiveFullScreenEffect(), false);

    // trigger animation
    Test::app()->base.space->virtual_desktop_manager->setCurrent(2);

    QCOMPARE(effects->activeFullScreenEffect(), effectMain);
    QCOMPARE(effects->hasActiveFullScreenEffect(), true);
    QCOMPARE(fullScreenEffectActiveSpy.count(), 1);

    QCOMPARE(effectMain->isActiveFullScreenEffect(), true);
    QCOMPARE(isActiveFullScreenEffectSpy.count(), 1);

    QCOMPARE(effectOther->isActiveFullScreenEffect(), false);
    QCOMPARE(isActiveFullScreenEffectSpyOther.count(), 0);

    // after 500ms trigger another full screen animation
    QTest::qWait(500);
    Test::app()->base.space->virtual_desktop_manager->setCurrent(1);
    QCOMPARE(effects->activeFullScreenEffect(), effectMain);

    // after 1000ms (+a safety margin for time based tests) we should still be the active full
    // screen effect despite first animation expiring
    QTest::qWait(500 + 100);
    QCOMPARE(effects->activeFullScreenEffect(), effectMain);

    // after 1500ms (+a safetey margin) we should have no full screen effect
    QTest::qWait(500 + 100);
    QCOMPARE(effects->activeFullScreenEffect(), nullptr);
}

void ScriptedEffectsTest::testKeepAlive_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<bool>("keepAlive");

    QTest::newRow("keep") << "keepAliveTest" << true;
    QTest::newRow("don't keep") << "keepAliveTestDontKeep" << false;
}

void ScriptedEffectsTest::testKeepAlive()
{
    // this test checks whether closed windows are kept alive
    // when keepAlive property is set to true(false)

    QFETCH(QString, file);
    QFETCH(bool, keepAlive);

    auto* effect = new ScriptedEffectWithDebugSpy;
    QSignalSpy effectOutputSpy(effect, &ScriptedEffectWithDebugSpy::testOutput);
    QVERIFY(effectOutputSpy.isValid());
    QVERIFY(effect->load(file));

    // create a window
    using namespace Wrapland::Client;
    auto surface = Test::create_surface();
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(Test::app()->base.space->active_client, c);

    // no active animations at the beginning
    QCOMPARE(effect->state().count(), 0);

    // trigger windowClosed signal
    surface.reset();
    QVERIFY(effectOutputSpy.count() == 1 || effectOutputSpy.wait());

    if (keepAlive) {
        QCOMPARE(effect->state().count(), 1);

        QTest::qWait(500);
        QCOMPARE(effect->state().count(), 1);

        QTest::qWait(500 + 100); // 100ms is extra safety margin
        QCOMPARE(effect->state().count(), 0);
    } else {
        // the test effect doesn't keep the window alive, so it should be
        // removed immediately
        QSignalSpy deletedRemovedSpy(Test::app()->base.space->qobject.get(),
                                     &win::space::qobject_t::window_deleted);
        QVERIFY(deletedRemovedSpy.isValid());
        QVERIFY(deletedRemovedSpy.count() == 1
                || deletedRemovedSpy.wait(100)); // 100ms is less than duration of the animation
        QCOMPARE(effect->state().count(), 0);
    }
}

void ScriptedEffectsTest::testGrab()
{
    // this test verifies that scripted effects can grab windows that are
    // not already grabbed

    // load the test effect
    auto effect = new ScriptedEffectWithDebugSpy;
    QSignalSpy effectOutputSpy(effect, &ScriptedEffectWithDebugSpy::testOutput);
    QVERIFY(effectOutputSpy.isValid());
    QVERIFY(effect->load(QStringLiteral("grabTest")));

    // create test client
    using namespace Wrapland::Client;
    auto surface = Test::create_surface();
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(Test::app()->base.space->active_client, c);

    // the test effect should grab the test client successfully
    QCOMPARE(effectOutputSpy.count(), 1);
    QCOMPARE(effectOutputSpy.first().first(), QStringLiteral("ok"));
    QCOMPARE(c->render->effect->data(WindowAddedGrabRole).value<void*>(), effect);
}

void ScriptedEffectsTest::testGrabAlreadyGrabbedWindow()
{
    // this test verifies that scripted effects cannot grab already grabbed
    // windows (unless force is set to true of course)

    // load effect that will hold the window grab
    auto owner = new ScriptedEffectWithDebugSpy;
    QSignalSpy ownerOutputSpy(owner, &ScriptedEffectWithDebugSpy::testOutput);
    QVERIFY(ownerOutputSpy.isValid());
    QVERIFY(owner->load(QStringLiteral("grabAlreadyGrabbedWindowTest_owner")));

    // load effect that will try to grab already grabbed window
    auto grabber = new ScriptedEffectWithDebugSpy;
    QSignalSpy grabberOutputSpy(grabber, &ScriptedEffectWithDebugSpy::testOutput);
    QVERIFY(grabberOutputSpy.isValid());
    QVERIFY(grabber->load(QStringLiteral("grabAlreadyGrabbedWindowTest_grabber")));

    // create test client
    using namespace Wrapland::Client;
    auto surface = Test::create_surface();
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(Test::app()->base.space->active_client, c);

    // effect that initially held the grab should still hold the grab
    QCOMPARE(ownerOutputSpy.count(), 1);
    QCOMPARE(ownerOutputSpy.first().first(), QStringLiteral("ok"));
    QCOMPARE(c->render->effect->data(WindowAddedGrabRole).value<void*>(), owner);

    // effect that tried to grab already grabbed window should fail miserably
    QCOMPARE(grabberOutputSpy.count(), 1);
    QCOMPARE(grabberOutputSpy.first().first(), QStringLiteral("fail"));
}

void ScriptedEffectsTest::testGrabAlreadyGrabbedWindowForced()
{
    // this test verifies that scripted effects can steal window grabs when
    // they forcefully try to grab windows

    // load effect that initially will be holding the window grab
    auto owner = new ScriptedEffectWithDebugSpy;
    QSignalSpy ownerOutputSpy(owner, &ScriptedEffectWithDebugSpy::testOutput);
    QVERIFY(ownerOutputSpy.isValid());
    QVERIFY(owner->load(QStringLiteral("grabAlreadyGrabbedWindowForcedTest_owner")));

    // load effect that will try to steal the window grab
    auto thief = new ScriptedEffectWithDebugSpy;
    QSignalSpy thiefOutputSpy(thief, &ScriptedEffectWithDebugSpy::testOutput);
    QVERIFY(thiefOutputSpy.isValid());
    QVERIFY(thief->load(QStringLiteral("grabAlreadyGrabbedWindowForcedTest_thief")));

    // create test client
    using namespace Wrapland::Client;
    auto surface = Test::create_surface();
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(Test::app()->base.space->active_client, c);

    // verify that the owner in fact held the grab
    QCOMPARE(ownerOutputSpy.count(), 1);
    QCOMPARE(ownerOutputSpy.first().first(), QStringLiteral("ok"));

    // effect that grabbed the test client forcefully should now hold the grab
    QCOMPARE(thiefOutputSpy.count(), 1);
    QCOMPARE(thiefOutputSpy.first().first(), QStringLiteral("ok"));
    QCOMPARE(c->render->effect->data(WindowAddedGrabRole).value<void*>(), thief);
}

void ScriptedEffectsTest::testUngrab()
{
    // this test verifies that scripted effects can ungrab windows that they
    // are previously grabbed

    // load the test effect
    auto effect = new ScriptedEffectWithDebugSpy;
    QSignalSpy effectOutputSpy(effect, &ScriptedEffectWithDebugSpy::testOutput);
    QVERIFY(effectOutputSpy.isValid());
    QVERIFY(effect->load(QStringLiteral("ungrabTest")));

    // create test client
    using namespace Wrapland::Client;
    auto surface = Test::create_surface();
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(Test::app()->base.space->active_client, c);

    // the test effect should grab the test client successfully
    QCOMPARE(effectOutputSpy.count(), 1);
    QCOMPARE(effectOutputSpy.first().first(), QStringLiteral("ok"));
    QCOMPARE(c->render->effect->data(WindowAddedGrabRole).value<void*>(), effect);

    // when the test effect sees that a window was minimized, it will try to ungrab it
    effectOutputSpy.clear();
    win::set_minimized(c, true);

    QCOMPARE(effectOutputSpy.count(), 1);
    QCOMPARE(effectOutputSpy.first().first(), QStringLiteral("ok"));
    QCOMPARE(c->render->effect->data(WindowAddedGrabRole).value<void*>(), nullptr);
}

void ScriptedEffectsTest::testRedirect_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<bool>("shouldTerminate");
    QTest::newRow("animate/DontTerminateAtSource") << "redirectAnimateDontTerminateTest" << false;
    QTest::newRow("animate/TerminateAtSource") << "redirectAnimateTerminateTest" << true;
    QTest::newRow("set/DontTerminate") << "redirectSetDontTerminateTest" << false;
    QTest::newRow("set/Terminate") << "redirectSetTerminateTest" << true;
}

void ScriptedEffectsTest::testRedirect()
{
    // this test verifies that redirect() works

    // load the test effect
    auto effect = new ScriptedEffectWithDebugSpy;
    QFETCH(QString, file);
    QVERIFY(effect->load(file));

    // create test client
    using namespace Wrapland::Client;
    auto surface = Test::create_surface();
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(Test::app()->base.space->active_client, c);

    auto around = [](std::chrono::milliseconds elapsed,
                     std::chrono::milliseconds pivot,
                     std::chrono::milliseconds margin) {
        return qAbs(elapsed.count() - pivot.count()) < margin.count();
    };

    // initially, the test animation is at the source position

    {
        const auto state = effect->state();
        QCOMPARE(state.count(), 1);
        QCOMPARE(state.firstKey(), c->render->effect.get());
        const QList<AniData> animations = state.first().first;
        QCOMPARE(animations.count(), 1);
        QTRY_COMPARE(animations[0].timeLine.direction(), TimeLine::Forward);
        QTRY_VERIFY(around(animations[0].timeLine.elapsed(), 0ms, 50ms));
    }

    // minimize the test client after 250ms, when the test effect sees that
    // a window was minimized, it will try to reverse animation for it
    QTest::qWait(250);

    QSignalSpy effectOutputSpy(effect, &ScriptedEffectWithDebugSpy::testOutput);
    QVERIFY(effectOutputSpy.isValid());

    win::set_minimized(c, true);

    QCOMPARE(effectOutputSpy.count(), 1);
    QCOMPARE(effectOutputSpy.first().first(), QStringLiteral("ok"));

    {
        const auto state = effect->state();
        QCOMPARE(state.count(), 1);
        QCOMPARE(state.firstKey(), c->render->effect.get());
        const QList<AniData> animations = state.first().first;
        QCOMPARE(animations.count(), 1);
        QCOMPARE(animations[0].timeLine.direction(), TimeLine::Backward);
        QVERIFY(around(animations[0].timeLine.elapsed(), 1000ms - 250ms, 50ms));
    }

    // wait for the animation to reach the start position, 100ms is an extra
    // safety margin
    QTest::qWait(250 + 100);

    QFETCH(bool, shouldTerminate);
    if (shouldTerminate) {
        const auto state = effect->state();
        QCOMPARE(state.count(), 0);
    } else {
        const auto state = effect->state();
        QCOMPARE(state.count(), 1);
        QCOMPARE(state.firstKey(), c->render->effect.get());
        const QList<AniData> animations = state.first().first;
        QCOMPARE(animations.count(), 1);
        QCOMPARE(animations[0].timeLine.direction(), TimeLine::Backward);
        QCOMPARE(animations[0].timeLine.elapsed(), 1000ms);
        QCOMPARE(animations[0].timeLine.value(), 0.0);
    }
}

void ScriptedEffectsTest::testComplete()
{
    // this test verifies that complete works

    // load the test effect
    auto effect = new ScriptedEffectWithDebugSpy;
    QVERIFY(effect->load(QStringLiteral("completeTest")));

    // create test client
    using namespace Wrapland::Client;
    auto surface = Test::create_surface();
    QVERIFY(surface);
    auto shellSurface = Test::create_xdg_shell_toplevel(surface);
    QVERIFY(shellSurface);
    auto c = Test::render_and_wait_for_shown(surface, QSize(100, 50), Qt::blue);
    QVERIFY(c);
    QCOMPARE(Test::app()->base.space->active_client, c);

    auto around = [](std::chrono::milliseconds elapsed,
                     std::chrono::milliseconds pivot,
                     std::chrono::milliseconds margin) {
        return qAbs(elapsed.count() - pivot.count()) < margin.count();
    };

    // initially, the test animation should be at the start position
    {
        const auto state = effect->state();
        QCOMPARE(state.count(), 1);
        QCOMPARE(state.firstKey(), c->render->effect.get());
        const QList<AniData> animations = state.first().first;
        QTRY_COMPARE(animations.count(), 1);
        QTRY_VERIFY(around(animations[0].timeLine.elapsed(), 0ms, 100ms));
        QTRY_VERIFY(!animations[0].timeLine.done());
    }

    // wait for 250ms
    QTest::qWait(250);

    {
        const auto state = effect->state();
        QCOMPARE(state.count(), 1);
        QCOMPARE(state.firstKey(), c->render->effect.get());
        const QList<AniData> animations = state.first().first;
        QCOMPARE(animations.count(), 1);
        QVERIFY(around(animations[0].timeLine.elapsed(), 250ms, 100ms));
        QVERIFY(!animations[0].timeLine.done());
    }

    // minimize the test client, when the test effect sees that a window was
    // minimized, it will try to complete animation for it
    QSignalSpy effectOutputSpy(effect, &ScriptedEffectWithDebugSpy::testOutput);
    QVERIFY(effectOutputSpy.isValid());

    win::set_minimized(c, true);

    QCOMPARE(effectOutputSpy.count(), 1);
    QCOMPARE(effectOutputSpy.first().first(), QStringLiteral("ok"));

    {
        const auto state = effect->state();
        QCOMPARE(state.count(), 1);
        QCOMPARE(state.firstKey(), c->render->effect.get());
        const QList<AniData> animations = state.first().first;
        QCOMPARE(animations.count(), 1);
        QCOMPARE(animations[0].timeLine.elapsed(), 1000ms);
        QVERIFY(animations[0].timeLine.done());
    }
}

}

WAYLANDTEST_MAIN(KWin::ScriptedEffectsTest)
#include "scripted_effects_test.moc"
