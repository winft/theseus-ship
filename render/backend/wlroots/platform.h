/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "egl_backend.h"
#include "output.h"
#include "qpainter_backend.h"
#include "wlr_helpers.h"

#include "base/options.h"
#include "render/wayland/platform.h"

#include <wayland_logging.h>

#include <variant>

namespace KWin::render::backend::wlroots
{

template<typename Base>
class platform : public wayland::platform<typename Base::abstract_type>
{
public:
    using type = platform<Base>;
    using abstract_type = wayland::platform<typename Base::abstract_type>;
    using compositor_t = typename abstract_type::compositor_t;
    using output_t = output<typename Base::output_t, type>;

    explicit platform(Base& base)
        : abstract_type(base)
        , base{base}
    {
    }

    void init()
    {
        // TODO(romangg): Has to be here because in the integration tests base.backend is not yet
        //                available in the ctor. Can we change that?
        if (kwinApp()->options->qobject->compositingMode() == QPainterCompositing) {
            qpainter = create_render_backend<qpainter_backend<platform>>(*this, "pixman");
        } else {
            egl = create_render_backend<egl_backend<platform>>(*this, "gles2");
        }

        if (!wlr_backend_start(base.backend)) {
            throw std::exception();
        }

        base::update_output_topology(base);
    }

    CompositingType selected_compositor() const override
    {
        if (qpainter) {
            return QPainterCompositing;
        }
        assert(egl);
        return OpenGLCompositing;
    }

    gl::backend<gl::scene<abstract_type>, abstract_type>*
    get_opengl_backend(compositor_t& /*compositor*/) override
    {
        assert(egl);
        egl->make_current();
        return egl.get();
    }

    qpainter::backend<qpainter::scene<abstract_type>>*
    get_qpainter_backend(compositor_t& /*compositor*/) override
    {
        assert(qpainter);
        return qpainter.get();
    }

    void render_stop(bool on_shutdown) override
    {
        if (egl && on_shutdown) {
            wayland::unbind_egl_display(*egl, egl->data);
            egl->tear_down();
        }
    }

    Base& base;
    std::unique_ptr<egl_backend<platform>> egl;
    std::unique_ptr<qpainter_backend<platform>> qpainter;

    wlr_renderer* renderer{nullptr};
    wlr_allocator* allocator{nullptr};

private:
    template<typename Render, typename Platform>
    std::unique_ptr<Render> create_render_backend(Platform& platform,
                                                  std::string const& wlroots_name)
    {
        setenv("WLR_RENDERER", wlroots_name.c_str(), true);
        platform.renderer = wlr_renderer_autocreate(platform.base.backend);
        platform.allocator = wlr_allocator_autocreate(platform.base.backend, platform.renderer);
        return std::make_unique<Render>(platform);
    }
};

}