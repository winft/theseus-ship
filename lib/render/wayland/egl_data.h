/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/gl/egl_data.h"

#include <epoxy/egl.h>
#include <wayland-server.h>

#include <QByteArray>
#include <QList>

namespace KWin::render::wayland
{

struct egl_data {
    using bind_wl_display_func = EGLBoolean (*)(EGLDisplay dpy, wl_display* display);
    using unbind_wl_display_func = EGLBoolean (*)(EGLDisplay dpy, wl_display* display);
    using query_wl_buffer_func = EGLBoolean (*)(EGLDisplay dpy,
                                                struct wl_resource* buffer,
                                                EGLint attribute,
                                                EGLint* value);

    bind_wl_display_func bind_wl_display{nullptr};
    unbind_wl_display_func unbind_wl_display{nullptr};
    query_wl_buffer_func query_wl_buffer{nullptr};

    gl::egl_data base;
};

}
