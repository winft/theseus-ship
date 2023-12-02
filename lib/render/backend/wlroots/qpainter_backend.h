/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "qpainter_output.h"
#include "wlr_includes.h"

#include "render/qpainter/backend.h"
#include "render/qpainter/scene.h"

namespace KWin::render::backend::wlroots
{

template<typename Platform>
class qpainter_backend : public qpainter::backend<qpainter::scene<typename Platform::frontend_type>>
{
public:
    using type = qpainter_backend<Platform>;
    using qpainter_scene = qpainter::scene<typename Platform::frontend_type>;
    using abstract_type = qpainter::backend<qpainter_scene>;
    using output_t = typename Platform::output_t;
    using qpainter_output_t = qpainter_output<output_t>;
    using base_output_t = typename Platform::frontend_type::base_t::output_t;

    qpainter_backend(Platform& platform)
        : qpainter::backend<qpainter_scene>()
        , platform{platform}
    {
        for (auto& out : platform.frontend->base.all_outputs) {
            auto render = static_cast<typename Platform::output_t*>(out->render.get());
            get_qpainter_output(*out)
                = std::make_unique<qpainter_output_t>(*render, platform.renderer);
        }
    }

    ~qpainter_backend() override
    {
        tear_down();
    }

    void begin_render(base_output_t& output) override
    {
        get_qpainter_output(output)->begin_render();
    }

    void present(base_output_t* output, QRegion const& damage) override
    {
        wlr_renderer_end(platform.renderer);
        get_qpainter_output(*output)->present(damage);
    }

    QImage* bufferForScreen(base_output_t* output) override
    {
        return get_qpainter_output(*output)->buffer.get();
    }

    bool needsFullRepaint() const override
    {
        return false;
    }

    void tear_down()
    {
    }

    Platform& platform;

private:
    static std::unique_ptr<qpainter_output_t>& get_qpainter_output(base_output_t& output)
    {
        auto& backend_output = static_cast<typename Platform::output_t&>(*output.render);
        return backend_output.qpainter;
    }
};

}
