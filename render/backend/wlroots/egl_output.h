/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "egl_helpers.h"
#include "wlr_includes.h"

#include "base/backend/wlroots/output.h"
#include "base/logging.h"
#include "render/wayland/egl_data.h"
#include "wayland_logging.h"

#include <kwingl/texture.h>
#include <kwingl/utils.h>

#include <QRegion>
#include <deque>
#include <epoxy/egl.h>
#include <memory>
#include <optional>

namespace KWin::render::backend::wlroots
{

template<typename Output>
class egl_output
{
public:
    egl_output(Output& out, wayland::egl_data egl_data)
        : out{&out}
        , egl_data{egl_data}
    {
        reset();
    }

    egl_output(egl_output const&) = delete;
    egl_output& operator=(egl_output const&) = delete;

    egl_output(egl_output&& other) noexcept
    {
        *this = std::move(other);
    }

    egl_output& operator=(egl_output&& other) noexcept
    {
        out = other.out;
        bufferAge = other.bufferAge;
        egl_data = std::move(other.egl_data);
        damageHistory = std::move(other.damageHistory);

        render = std::move(other.render);
        other.render = {};

        return *this;
    }

    ~egl_output()
    {
        cleanup_framebuffer();
    }

    bool reset()
    {
        reset_framebuffer();
        return true;
    }

    bool reset_framebuffer()
    {
        cleanup_framebuffer();

        auto const view_geo = out->base.view_geometry();
        auto const centered_view
            = out->base.mode_size() != view_geo.size() || !view_geo.topLeft().isNull();

        if (out->base.transform() == base::wayland::output_transform::normal && !centered_view) {
            // No need to create intermediate framebuffer.
            return true;
        }

        // TODO(romangg): Also return in case wlroots can rotate in hardware.

        make_current();

        auto const texSize = view_geo.size();
        render.texture = GLTexture(GL_TEXTURE_2D, texSize.width(), texSize.height());
        render.fbo = GLRenderTarget(render.texture.value());
        return render.fbo.valid();
    }

    void cleanup_framebuffer()
    {
        if (!render.fbo.valid()) {
            return;
        }
        make_current();
        render.texture = {};
        render.fbo = {};
    }

    void make_current() const
    {
        make_context_current(egl_data);
    }

    bool present()
    {
        auto& base = static_cast<base::backend::wlroots::output&>(out->base);
        out->swap_pending = true;

        if (!base.native->enabled) {
            wlr_output_enable(base.native, true);
        }

        if (!wlr_output_test(base.native)) {
            qCWarning(KWIN_CORE) << "Atomic output test failed on present.";
            wlr_output_rollback(base.native);
            reset();
            return false;
        }
        if (!wlr_output_commit(base.native)) {
            qCWarning(KWIN_CORE) << "Atomic output commit failed on present.";
            reset();
            return false;
        }
        return true;
    }

    Output* out;
    int bufferAge{0};
    wayland::egl_data egl_data;

    /** Damage history for the past 10 frames. */
    std::deque<QRegion> damageHistory;

    struct {
        GLRenderTarget fbo;
        std::optional<GLTexture> texture;
        std::shared_ptr<GLVertexBuffer> vbo;
    } render;
};

}
