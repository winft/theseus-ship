/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "generic_scene_opengl_test.h"

#include "base/wayland/server.h"
#include "render/compositor.h"
#include "render/effect_loader.h"
#include "render/platform.h"
#include "render/scene.h"

#include "win/wayland/window.h"

#include <KConfigGroup>

namespace KWin
{

GenericSceneOpenGLTest::GenericSceneOpenGLTest(const QByteArray& envVariable)
    : QObject()
    , m_envVariable(envVariable)
{
}

GenericSceneOpenGLTest::~GenericSceneOpenGLTest()
{
}

void GenericSceneOpenGLTest::cleanup()
{
    Test::destroy_wayland_connection();
}

void GenericSceneOpenGLTest::initTestCase()
{
    QSignalSpy startup_spy(Test::app(), &WaylandTestApplication::startup_finished);
    QVERIFY(startup_spy.isValid());

    // disable all effects - we don't want to have it interact with the rendering
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    auto const builtinNames
        = render::effect_loader(*effects, *Test::app()->base->render->compositor)
              .listOfKnownEffects();
    for (const QString& name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }

    config->sync();
    Test::app()->base->config.main = config;

    qputenv("XCURSOR_THEME", QByteArrayLiteral("DMZ-White"));
    qputenv("XCURSOR_SIZE", QByteArrayLiteral("24"));
    qputenv("KWIN_COMPOSE", m_envVariable);

    Test::app()->start();
    QVERIFY(startup_spy.size() || startup_spy.wait());
    QVERIFY(Test::app()->base->render->compositor);

    auto& scene = Test::app()->base->render->compositor->scene;
    QVERIFY(scene);
    QCOMPARE(scene->compositingType(), KWin::OpenGLCompositing);
    QCOMPARE(Test::app()->base->render->selected_compositor(), KWin::OpenGLCompositing);
}

void GenericSceneOpenGLTest::testRestart()
{
    // simple restart of the OpenGL compositor without any windows being shown
    Test::app()->base->render->compositor->reinitialize();

    auto& scene = Test::app()->base->render->compositor->scene;
    QVERIFY(scene);
    QCOMPARE(scene->compositingType(), KWin::OpenGLCompositing);
    QCOMPARE(Test::app()->base->render->selected_compositor(), KWin::OpenGLCompositing);

    // trigger a repaint
    render::full_repaint(*Test::app()->base->render->compositor);
    // and wait 100 msec to ensure it's rendered
    // TODO: introduce frameRendered signal in SceneOpenGL
    QTest::qWait(100);
}

}
