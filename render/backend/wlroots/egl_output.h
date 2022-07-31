/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"
#include "render/wayland/egl_data.h"

#include <kwingl/texture.h>
#include <kwingl/utils.h>

#include <QRegion>
#include <deque>
#include <epoxy/egl.h>
#include <memory>
#include <optional>

namespace KWin::render::backend::wlroots
{

class output;

class KWIN_EXPORT egl_output
{
public:
    egl_output(output& out, wayland::egl_data egl_data);
    egl_output(egl_output const&) = delete;
    egl_output& operator=(egl_output const&) = delete;
    egl_output(egl_output&& other) noexcept;
    egl_output& operator=(egl_output&& other) noexcept;
    ~egl_output();

    bool reset();
    bool reset_framebuffer();
    void cleanup_framebuffer();

    void make_current() const;
    bool present();

    output* out;
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
