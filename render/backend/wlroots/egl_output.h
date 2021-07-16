/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QRegion>

#include <deque>
#include <memory>

#include <epoxy/egl.h>
#include <kwingltexture.h>

namespace KWin::render::backend::wlroots
{

class buffer;
class egl_backend;
class output;
class surface;

class egl_output
{
public:
    output* out;
    std::unique_ptr<surface> surf;
    int bufferAge{0};
    egl_backend* egl_back;

    /** Damage history for the past 10 frames. */
    std::deque<QRegion> damageHistory;

    struct {
        GLuint framebuffer = 0;
        GLuint texture = 0;
        std::shared_ptr<GLVertexBuffer> vbo;
    } render;

    egl_output(output* out, egl_backend* egl_back);
    egl_output(egl_output const&) = delete;
    egl_output& operator=(egl_output const&) = delete;
    egl_output(egl_output&& other) noexcept;
    egl_output& operator=(egl_output&& other) noexcept;
    ~egl_output();

    bool reset(output* out);

    bool reset_framebuffer();
    void cleanup_framebuffer();

    bool make_current() const;
    bool present(buffer* buf);

    buffer* create_buffer();
};

}
