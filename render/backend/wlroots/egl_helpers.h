/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/wayland/egl_data.h"

namespace KWin::render::backend::wlroots
{

inline void load_egl_proc(void* proc_ptr, const char* name)
{
    void* proc = (void*)eglGetProcAddress(name);
    *(void**)proc_ptr = proc;
}

inline void make_context_current(wayland::egl_data const& data)
{
    eglMakeCurrent(data.base.display, EGL_NO_SURFACE, EGL_NO_SURFACE, data.base.context);
}

inline void unset_context_current(wayland::egl_data const& data)
{
    eglMakeCurrent(data.base.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

inline bool is_context_current(wayland::egl_data const& data)
{
    return eglGetCurrentContext() == data.base.context;
}

using eglFuncPtr = void (*)();
inline eglFuncPtr get_proc_address(char const* name)
{
    return eglGetProcAddress(name);
}

}
