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

    surface(gbm_surface* gbm_surf, EGLSurface egl_surf, EGLDisplay egl_display)
        : egl_display{egl_display}
        , gbm{gbm_surf}
        , egl{egl_surf}
    {
    }

    ~surface()
    {
        for (auto buf : buffers) {
            buf->surf = nullptr;
        }
        eglDestroySurface(egl_display, egl);
        gbm_surface_destroy(gbm);
    }

    surface(surface const&) = delete;
    surface& operator=(surface const&) = delete;
    surface(surface&&) noexcept = default;
    surface& operator=(surface&&) noexcept = default;
};

}
