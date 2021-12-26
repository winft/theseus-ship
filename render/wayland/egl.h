/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "main.h"
#include "render/gl/egl_dmabuf.h"
#include "wayland_server.h"

#include <Wrapland/Server/display.h>
#include <wayland-server.h>

namespace KWin::render::wayland
{

using eglBindWaylandDisplayWL_func = GLboolean (*)(EGLDisplay dpy, wl_display* display);

using eglUnbindWaylandDisplayWL_func = GLboolean (*)(EGLDisplay dpy, wl_display* display);
using eglQueryWaylandBufferWL_func
    = GLboolean (*)(EGLDisplay dpy, struct wl_resource* buffer, EGLint attribute, EGLint* value);

extern eglBindWaylandDisplayWL_func eglBindWaylandDisplayWL;
extern eglUnbindWaylandDisplayWL_func eglUnbindWaylandDisplayWL;
extern eglQueryWaylandBufferWL_func eglQueryWaylandBufferWL;

template<typename Backend>
void init_egl(Backend& backend)
{
    if (!kwinApp()->get_wayland_server()) {
        return;
    }

    if (backend.hasExtension(QByteArrayLiteral("EGL_WL_bind_wayland_display"))) {
        eglBindWaylandDisplayWL = reinterpret_cast<eglBindWaylandDisplayWL_func>(
            eglGetProcAddress("eglBindWaylandDisplayWL"));
        eglUnbindWaylandDisplayWL = reinterpret_cast<eglUnbindWaylandDisplayWL_func>(
            eglGetProcAddress("eglUnbindWaylandDisplayWL"));
        eglQueryWaylandBufferWL = reinterpret_cast<eglQueryWaylandBufferWL_func>(
            eglGetProcAddress("eglQueryWaylandBufferWL"));

        // only bind if not already done
        if (auto wl_display = waylandServer()->display();
            wl_display->eglDisplay() != backend.eglDisplay()) {
            if (!eglBindWaylandDisplayWL(backend.eglDisplay(), wl_display->native())) {
                eglUnbindWaylandDisplayWL = nullptr;
                eglQueryWaylandBufferWL = nullptr;
            } else {
                wl_display->setEglDisplay(backend.eglDisplay());
            }
        }
    }

    assert(!backend.dmabuf);
    backend.dmabuf = gl::egl_dmabuf::factory(&backend);
}

template<typename Backend>
void unbind_egl_display(Backend& backend)
{
    if (eglUnbindWaylandDisplayWL && backend.eglDisplay() != EGL_NO_DISPLAY) {
        eglUnbindWaylandDisplayWL(backend.eglDisplay(),
                                  kwinApp()->get_wayland_server()->display()->native());
    }
}

}
