/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"
#include "render/gl/backend.h"
#include "render/gl/texture.h"

#include <epoxy/glx.h>
#include <fixx11h.h>
#include <memory>
#include <xcb/glx.h>

namespace KWin::render
{

namespace x11
{
class overlay_window;
}

class compositor;

namespace backend::x11
{

// GLX_MESA_swap_interval
using glXSwapIntervalMESA_func = int (*)(unsigned int interval);
extern glXSwapIntervalMESA_func glXSwapIntervalMESA;

class fb_config_info
{
public:
    GLXFBConfig fbconfig;
    int bind_texture_format;
    int texture_targets;
    int y_inverted;
    int mipmap;
};

// ------------------------------------------------------------------

class swap_event_filter : public base::x11::event_filter
{
public:
    swap_event_filter(xcb_drawable_t drawable, xcb_glx_drawable_t glxDrawable);
    bool event(xcb_generic_event_t* event) override;

private:
    xcb_drawable_t m_drawable;
    xcb_glx_drawable_t m_glxDrawable;
};

/**
 * @brief OpenGL Backend using GLX over an X overlay window.
 */
class glx_backend : public gl::backend
{
public:
    glx_backend(Display* display, render::compositor* compositor);
    ~glx_backend() override;
    void screenGeometryChanged(const QSize& size) override;
    gl::texture_private* createBackendTexture(gl::texture* texture) override;
    QRegion prepareRenderingFrame() override;
    void endRenderingFrame(const QRegion& damage, const QRegion& damagedRegion) override;
    bool makeCurrent() override;
    void doneCurrent() override;
    bool hasSwapEvent() const override;
    void init() override;

protected:
    void present() override;

private:
    bool initBuffer();
    bool checkVersion();
    void initExtensions();
    bool initRenderingContext();
    bool initFbConfig();
    void initVisualDepthHashTable();
    Display* display() const
    {
        return m_x11Display;
    }
    bool supportsSwapEvents() const;

    int visualDepth(xcb_visualid_t visual) const;
    fb_config_info* infoForVisual(xcb_visualid_t visual);

    std::unique_ptr<render::x11::overlay_window> overlay_window;
    Window window;
    GLXFBConfig fbconfig;
    GLXWindow glxWindow;
    GLXContext ctx;
    QHash<xcb_visualid_t, fb_config_info*> m_fbconfigHash;
    QHash<xcb_visualid_t, int> m_visualDepthHash;
    std::unique_ptr<swap_event_filter> swap_filter;
    int m_bufferAge;
    bool m_haveMESACopySubBuffer = false;
    bool m_haveMESASwapControl = false;
    bool m_haveEXTSwapControl = false;
    bool m_needsCompositeTimerStart = false;
    Display* m_x11Display;
    render::compositor* compositor;

    friend class GlxTexture;
};

/**
 * @brief Texture using an GLXPixmap.
 */
class GlxTexture : public gl::texture_private
{
public:
    ~GlxTexture() override;
    void onDamage() override;
    bool loadTexture(render::window_pixmap* pixmap) override;
    gl::backend* backend() override;

private:
    friend class glx_backend;
    GlxTexture(gl::texture* texture, glx_backend* backend);

    bool loadTexture(xcb_pixmap_t pix, const QSize& size, xcb_visualid_t visual);
    Display* display() const
    {
        return m_backend->m_x11Display;
    }

    gl::texture* q;
    glx_backend* m_backend;

    // the glx pixmap the texture is bound to
    GLXPixmap m_glxpixmap;
};

}
}
