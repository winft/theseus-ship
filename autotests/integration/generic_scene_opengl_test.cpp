/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>

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
#include "generic_scene_opengl_test.h"
#include "effect_builtins.h"
#include "effectloader.h"
#include "platform.h"
#include "render/compositor.h"
#include "scene.h"
#include "wayland_server.h"

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
    qRegisterMetaType<win::wayland::window*>();

    QSignalSpy workspaceCreatedSpy(kwinApp(), &Application::workspaceCreated);
    QVERIFY(workspaceCreatedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));

    // disable all effects - we don't want to have it interact with the rendering
    auto config = KSharedConfig::openConfig(QString(), KConfig::SimpleConfig);
    KConfigGroup plugins(config, QStringLiteral("Plugins"));
    ScriptedEffectLoader loader;
    const auto builtinNames = BuiltInEffects::availableEffectNames() << loader.listOfKnownEffects();
    for (QString name : builtinNames) {
        plugins.writeEntry(name + QStringLiteral("Enabled"), false);
    }

    config->sync();
    kwinApp()->setConfig(config);

    qputenv("XCURSOR_THEME", QByteArrayLiteral("DMZ-White"));
    qputenv("XCURSOR_SIZE", QByteArrayLiteral("24"));
    qputenv("KWIN_COMPOSE", m_envVariable);

    kwinApp()->start();
    QVERIFY(workspaceCreatedSpy.size() || workspaceCreatedSpy.wait());
    QVERIFY(render::compositor::self());

    auto scene = render::compositor::self()->scene();
    QVERIFY(scene);
    QCOMPARE(scene->compositingType(), KWin::OpenGL2Compositing);
    QCOMPARE(kwinApp()->platform()->selectedCompositor(), KWin::OpenGLCompositing);
}

void GenericSceneOpenGLTest::testRestart_data()
{
    QTest::addColumn<bool>("core");

    QTest::newRow("GLCore") << true;
    QTest::newRow("Legacy") << false;
}

void GenericSceneOpenGLTest::testRestart()
{
    // simple restart of the OpenGL compositor without any windows being shown

    // setup opengl compositing options
    auto compositingGroup = kwinApp()->config()->group("Compositing");
    QFETCH(bool, core);
    compositingGroup.writeEntry("GLCore", core);
    compositingGroup.sync();

    QSignalSpy sceneCreatedSpy(render::compositor::self(), &render::compositor::sceneCreated);
    QVERIFY(sceneCreatedSpy.isValid());
    render::compositor::self()->reinitialize();
    if (sceneCreatedSpy.isEmpty()) {
        QVERIFY(sceneCreatedSpy.wait());
    }
    QCOMPARE(sceneCreatedSpy.count(), 1);
    auto scene = render::compositor::self()->scene();
    QVERIFY(scene);
    QCOMPARE(scene->compositingType(), KWin::OpenGL2Compositing);
    QCOMPARE(kwinApp()->platform()->selectedCompositor(), KWin::OpenGLCompositing);

    // trigger a repaint
    render::compositor::self()->addRepaintFull();
    // and wait 100 msec to ensure it's rendered
    // TODO: introduce frameRendered signal in SceneOpenGL
    QTest::qWait(100);
}

}
