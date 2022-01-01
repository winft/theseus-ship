/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "buffer.h"
#include "egl_backend.h"
#include "output.h"
#include "wlr_helpers.h"

#include "base/backend/wlroots/output.h"
#include "input/wayland/platform.h"
#include "render/wayland/compositor.h"
#include "render/wayland/effects.h"
#include "render/wayland/egl.h"
#include "screens.h"

#include <wayland_logging.h>

namespace KWin::render::backend::wlroots
{

platform::platform(base::backend::wlroots::platform& base)
    : render::platform(base)
    , base{base}
{
}

platform::~platform()
{
    if (egl_display_to_terminate != EGL_NO_DISPLAY) {
        eglTerminate(egl_display_to_terminate);
    }
}

void platform::init()
{
    // TODO(romangg): Has to be here because in the integration tests base.backend is not yet
    //                available in the ctor. Can we change that?
#if HAVE_WLR_OUTPUT_INIT_RENDER
    renderer = wlr_renderer_autocreate(base.backend);
    allocator = wlr_allocator_autocreate(base.backend, renderer);
#endif

    if (!wlr_backend_start(base.backend)) {
        throw std::exception();
    }

    base.screens.updateAll();
}

gl::backend* platform::createOpenGLBackend(render::compositor& /*compositor*/)
{
    if (!egl) {
        egl = std::make_unique<egl_backend>(
            *this, base::backend::wlroots::get_headless_backend(base.backend));
    }
    return egl.get();
}

void platform::render_stop(bool on_shutdown)
{
    assert(egl);
    if (on_shutdown) {
        wayland::unbind_egl_display(*egl, egl->data);
        egl->tear_down();
    }
}

void platform::createEffectsHandler(render::compositor* compositor, render::scene* scene)
{
    new wayland::effects_handler_impl(compositor, scene);
}

QVector<CompositingType> platform::supportedCompositors() const
{
    if (selected_compositor != NoCompositing) {
        return {selected_compositor};
    }
    return QVector<CompositingType>{OpenGLCompositing};
}

}
