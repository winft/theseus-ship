/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "generic_scene_opengl_test.h"

namespace KWin::detail::test
{

TEST_CASE("scene opengl", "[render]")
{
    auto setup = generic_scene_opengl_get_setup("scene-opengl", "O2");

    SECTION("restart")
    {
        // simple restart of the OpenGL compositor without any windows being shown
        setup->base->render->compositor->reinitialize();

        auto& scene = setup->base->render->compositor->scene;
        QVERIFY(scene);
        QCOMPARE(scene->compositingType(), KWin::OpenGLCompositing);
        QCOMPARE(setup->base->render->selected_compositor(), KWin::OpenGLCompositing);

        // trigger a repaint
        render::full_repaint(*setup->base->render->compositor);

        // and wait 100 msec to ensure it's rendered
        // TODO: introduce frameRendered signal in SceneOpenGL
        QTest::qWait(100);
    }
}

}
