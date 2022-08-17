/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/options.h"
#include "render/gl/backend.h"
#include "render/gl/texture.h"

// Must be included late because of Qt.
#include "glx_data.h"
#include "glx_fb_config.h"

#include <epoxy/glx.h>
#include <fixx11h.h>
#include <memory>
#include <unordered_map>
#include <xcb/glx.h>

namespace KWin::render
{

namespace x11
{
template<typename Platform>
class compositor;
class overlay_window;
class platform;
}

namespace backend::x11
{

/**
 * @brief OpenGL Backend using GLX over an X overlay window.
 */
class glx_backend : public gl::backend
{
public:
    using backend_t = gl::backend;

    glx_backend(Display* display, render::x11::compositor<render::x11::platform>& compositor);
    ~glx_backend() override;
    void screenGeometryChanged(const QSize& size) override;
    gl::texture_private<gl::backend>*
    createBackendTexture(gl::texture<gl::backend>* texture) override;
    QRegion prepareRenderingFrame() override;
    void endRenderingFrame(const QRegion& damage, const QRegion& damagedRegion) override;
    bool makeCurrent() override;
    void doneCurrent() override;
    bool hasSwapEvent() const override;

    int visualDepth(xcb_visualid_t visual) const;

    glx_data data;

    Window window{None};
    std::unique_ptr<render::x11::overlay_window> overlay_window;
    std::unordered_map<xcb_visualid_t, fb_config_info*> fb_configs;
    std::unordered_map<xcb_visualid_t, int> visual_depth_hash;

    render::x11::compositor<render::x11::platform>& compositor;

protected:
    void present() override;

private:
    bool supportsSwapEvents() const;

    GLRenderTarget native_fbo;
    int m_bufferAge{0};
    bool m_needsCompositeTimerStart = false;
};

}
}
