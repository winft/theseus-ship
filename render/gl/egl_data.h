/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <epoxy/egl.h>
#include <wayland-server.h>

#include <QByteArray>
#include <QList>

namespace KWin::render::gl
{

struct egl_data {
    EGLDisplay display{EGL_NO_DISPLAY};
    EGLSurface surface{EGL_NO_SURFACE};
    EGLContext context{EGL_NO_CONTEXT};
    EGLConfig config{nullptr};

    QList<QByteArray> client_extensions;
};

}
