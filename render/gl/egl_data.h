/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <epoxy/egl.h>

#include <QByteArray>
#include <QList>

namespace KWin::render::gl
{

struct egl_data {
    EGLDisplay display{EGL_NO_DISPLAY};
    EGLContext context{EGL_NO_CONTEXT};

    using create_image_khr_func = EGLImageKHR (*)(EGLDisplay dpy,
                                                  EGLContext ctx,
                                                  EGLenum target,
                                                  EGLClientBuffer buffer,
                                                  const EGLint* attrib_list);
    using destroy_image_khr_func = EGLBoolean (*)(EGLDisplay dpy, EGLImageKHR image);

    create_image_khr_func create_image_khr{nullptr};
    destroy_image_khr_func destroy_image_khr{nullptr};

    QList<QByteArray> client_extensions;
};

}
