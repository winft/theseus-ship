/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwinglobals.h"
#include <kwingl/platform.h>
#include <kwingl/utils.h>

#include <epoxy/egl.h>

namespace KWin::render::gl
{

template<typename ProcAddrGetter>
void init_gl(OpenGLPlatformInterface interface, ProcAddrGetter* getter)
{
    auto glPlatform = GLPlatform::instance();
    glPlatform->detect(interface);
    glPlatform->printResults();
    initGL(getter);
}

}
