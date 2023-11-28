/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "egl_backend.h"
#include "output.h"
#include "qpainter_backend.h"
#include "wlr_helpers.h"
#include <render/compositor_start.h>

#include <gsl/pointers>
#include <variant>

namespace KWin::render::backend::wlroots
{

template<typename Frontend>
class backend
{
public:
    using type = backend<Frontend>;
    using frontend_type = Frontend;
    using output_t = output<typename frontend_type::base_t::backend_t::output_t, type>;

    explicit backend(Frontend& frontend)
        : frontend{&frontend}
    {
    }

    void init()
    {
        // TODO(romangg): Has to be here because in the integration tests base.backend is not yet
        //                available in the ctor. Can we change that?
        if (frontend->options->qobject->sw_compositing()) {
            qpainter = create_render_backend<qpainter_backend<backend>>(*this, "pixman");
        } else {
            egl = create_render_backend<egl_backend<backend>>(*this, "gles2");
        }

        if (!wlr_backend_start(frontend->base.backend.native)) {
            throw std::exception();
        }

        base::update_output_topology(frontend->base);
    }

    bool is_sw_compositing() const
    {
        return static_cast<bool>(qpainter);
    }

    gl::backend<gl::scene<frontend_type>, frontend_type>* get_opengl_backend()
    {
        assert(egl);
        egl->make_current();
        return egl.get();
    }

    qpainter::backend<qpainter::scene<frontend_type>>* get_qpainter_backend()
    {
        assert(qpainter);
        return qpainter.get();
    }

    void render_stop(bool on_shutdown)
    {
        if (egl && on_shutdown) {
            wayland::unbind_egl_display(*egl, egl->data);
            egl->tear_down();
        }
    }

    gsl::not_null<Frontend*> frontend;
    std::unique_ptr<egl_backend<backend>> egl;
    std::unique_ptr<qpainter_backend<backend>> qpainter;

    wlr_renderer* renderer{nullptr};
    wlr_allocator* allocator{nullptr};

private:
    template<typename Render, typename Backend>
    std::unique_ptr<Render> create_render_backend(Backend& backend, std::string const& wlroots_name)
    {
        setenv("WLR_RENDERER", wlroots_name.c_str(), true);
        backend.renderer = wlr_renderer_autocreate(backend.frontend->base.backend.native);
        backend.allocator
            = wlr_allocator_autocreate(backend.frontend->base.backend.native, backend.renderer);
        return std::make_unique<Render>(backend);
    }
};

}
