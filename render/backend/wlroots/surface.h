/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "buffer.h"

#include <epoxy/egl.h>
#include <gbm.h>

#include <cassert>
#include <vector>

namespace KWin::render::backend::wlroots
{

class surface
{
private:
    EGLDisplay egl_display;

public:
    std::vector<buffer*> buffers;
    gbm_surface* gbm{nullptr};
    EGLSurface egl{EGL_NO_SURFACE};
    QSize size;

    surface(gbm_surface* gbm_surf, EGLSurface egl_surf, EGLDisplay egl_display, QSize const& size)
        : egl_display{egl_display}
        , gbm{gbm_surf}
        , egl{egl_surf}
        , size{size}
    {
    }

    ~surface()
    {
        for (auto buf : buffers) {
            buf->surf = nullptr;
        }
        eglDestroySurface(egl_display, egl);
        if (gbm) {
            gbm_surface_destroy(gbm);
        }
    }

    surface(surface const&) = delete;
    surface& operator=(surface const&) = delete;
    surface(surface&&) noexcept = default;
    surface& operator=(surface&&) noexcept = default;
};

}
