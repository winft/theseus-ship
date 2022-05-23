/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "egl_backend.h"
#include "output.h"
#include "qpainter_backend.h"
#include "wlr_helpers.h"

#include "base/backend/wlroots/output.h"
#include "base/output_helpers.h"
#include "input/wayland/platform.h"
#include "render/utils.h"
#include "render/wayland/compositor.h"
#include "render/wayland/effects.h"
#include "render/wayland/egl.h"

#include <wayland_logging.h>

namespace KWin::render::backend::wlroots
{

platform::platform(base::backend::wlroots::platform& base)
    : render::platform(base)
    , base{base}
{
}

platform::~platform() = default;

template<typename Render>
std::unique_ptr<Render> create_render_backend(wlroots::platform& platform,
                                              std::string const& wlroots_name)
{
    setenv("WLR_RENDERER", wlroots_name.c_str(), true);
    platform.renderer = wlr_renderer_autocreate(platform.base.backend);
    platform.allocator = wlr_allocator_autocreate(platform.base.backend, platform.renderer);
    return std::make_unique<Render>(platform);
}

void platform::init()
{
    // TODO(romangg): Has to be here because in the integration tests base.backend is not yet
    //                available in the ctor. Can we change that?
    auto const supported_types = get_supported_render_types(*this);
    for (auto render_type : supported_types) {
        if (render_type == OpenGLCompositing) {
            egl = create_render_backend<egl_backend>(*this, "gles2");
            break;
        }
        if (render_type == QPainterCompositing) {
            qpainter = create_render_backend<qpainter_backend>(*this, "pixman");
            break;
        }
    }

    if (!wlr_backend_start(base.backend)) {
        throw std::exception();
    }

    base::update_output_topology(base);
}

gl::backend* platform::createOpenGLBackend(render::compositor& /*compositor*/)
{
    assert(egl);
    wlr_egl_make_current(egl->native);
    return egl.get();
}

qpainter::backend* platform::createQPainterBackend(render::compositor& /*compositor*/)
{
    assert(qpainter);
    return qpainter.get();
}

void platform::render_stop(bool on_shutdown)
{
    if (egl && on_shutdown) {
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
    return QVector<CompositingType>{OpenGLCompositing, QPainterCompositing};
}

}
