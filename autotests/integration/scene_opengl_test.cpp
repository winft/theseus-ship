/*
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "generic_scene_opengl_test.h"

namespace KWin
{

class SceneOpenGLTest : public GenericSceneOpenGLTest
{
    Q_OBJECT
public:
    SceneOpenGLTest()
        : GenericSceneOpenGLTest(QByteArrayLiteral("O2"))
    {
    }
};

}

WAYLANDTEST_MAIN(KWin::SceneOpenGLTest)
#include "scene_opengl_test.moc"
