/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/gl/backend.h"
#include "render/gl/texture.h"

// Must be included late because of Qt.
#include "glx_data.h"

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

class fb_config_info;

/**
 * @brief OpenGL Backend using GLX over an X overlay window.
 */
class glx_backend : public gl::backend
{
public:
    glx_backend(Display* display, render::x11::compositor<render::x11::platform>& compositor);
    ~glx_backend() override;
    void screenGeometryChanged(const QSize& size) override;
    gl::texture_private* createBackendTexture(gl::texture* texture) override;
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

/**
 * @brief Texture using an GLXPixmap.
 */
class GlxTexture : public gl::texture_private
{
public:
    ~GlxTexture() override;
    void onDamage() override;
    bool updateTexture(render::buffer* pixmap) override;
    gl::backend* backend() override;

private:
    friend class glx_backend;
    GlxTexture(gl::texture* texture, glx_backend* backend);

    Display* display() const
    {
        return m_backend->data.display;
    }

    gl::texture* q;
    glx_backend* m_backend;

    // the glx pixmap the texture is bound to
    GLXPixmap m_glxpixmap;
};

}
}
