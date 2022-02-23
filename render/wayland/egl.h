/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "egl_data.h"

#include "base/wayland/server.h"
#include "main.h"
#include "render/gl/egl_dmabuf.h"

#include <Wrapland/Server/display.h>

namespace KWin::render::wayland
{

template<typename Backend>
void init_egl(Backend& backend, egl_data& egl)
{
    if (!kwinApp()->get_wayland_server()) {
        return;
    }

    if (backend.hasExtension(QByteArrayLiteral("EGL_WL_bind_wayland_display"))) {
        egl.bind_wl_display = reinterpret_cast<egl_data::bind_wl_display_func>(
            eglGetProcAddress("eglBindWaylandDisplayWL"));
        egl.unbind_wl_display = reinterpret_cast<egl_data::unbind_wl_display_func>(
            eglGetProcAddress("eglUnbindWaylandDisplayWL"));
        egl.query_wl_buffer = reinterpret_cast<egl_data::query_wl_buffer_func>(
            eglGetProcAddress("eglQueryWaylandBufferWL"));

        // only bind if not already done
        if (auto&& display = waylandServer()->display;
            display->eglDisplay() != backend.data.base.display) {
            if (!egl.bind_wl_display(backend.data.base.display, display->native())) {
                egl.unbind_wl_display = nullptr;
                egl.query_wl_buffer = nullptr;
            } else {
                display->setEglDisplay(backend.data.base.display);
            }
        }
    }

    assert(!backend.dmabuf);
    backend.dmabuf = gl::egl_dmabuf_factory(backend);
}

template<typename Backend>
void unbind_egl_display(Backend& backend, egl_data const& egl)
{
    if (egl.unbind_wl_display && backend.data.base.display != EGL_NO_DISPLAY) {
        egl.unbind_wl_display(backend.data.base.display,
                              kwinApp()->get_wayland_server()->display->native());
    }
}

}
