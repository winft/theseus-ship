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
void init_gl(gl_interface interface, ProcAddrGetter* getter, xcb_connection_t* x11_con)
{
    auto glPlatform = GLPlatform::create(x11_con);
    glPlatform->detect(interface);
    glPlatform->printResults();
    initGL(getter);
}

}
