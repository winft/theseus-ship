/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "egl_data.h"

#include "base/wayland/server.h"

#include <Wrapland/Server/display.h>

namespace KWin::render::wayland
{

template<typename EglBackend>
void init_egl(EglBackend& egl_backend, egl_data& egl)
{
    if (!egl_backend.hasExtension(QByteArrayLiteral("EGL_WL_bind_wayland_display"))) {
        return;
    }

    egl.bind_wl_display = reinterpret_cast<egl_data::bind_wl_display_func>(
        eglGetProcAddress("eglBindWaylandDisplayWL"));
    egl.unbind_wl_display = reinterpret_cast<egl_data::unbind_wl_display_func>(
        eglGetProcAddress("eglUnbindWaylandDisplayWL"));
    egl.query_wl_buffer = reinterpret_cast<egl_data::query_wl_buffer_func>(
        eglGetProcAddress("eglQueryWaylandBufferWL"));

    // only bind if not already done
    if (auto&& display = egl_backend.backend.frontend->base.server->display;
        display->eglDisplay() != egl_backend.data.base.display) {
        if (!egl.bind_wl_display(egl_backend.data.base.display, display->native())) {
            egl.unbind_wl_display = nullptr;
            egl.query_wl_buffer = nullptr;
        } else {
            display->setEglDisplay(egl_backend.data.base.display);
        }
    }
}

template<typename EglBackend>
void unbind_egl_display(EglBackend& egl_backend, egl_data const& egl)
{
    if (egl.unbind_wl_display && egl_backend.data.base.display != EGL_NO_DISPLAY) {
        egl.unbind_wl_display(egl_backend.data.base.display,
                              egl_backend.backend.frontend->base.server->display->native());
    }
}

}
