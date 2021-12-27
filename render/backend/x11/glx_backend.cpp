/*
    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

    Based on glcompmgr code by Felix Bellaby.
    Using code from Compiz and Beryl.

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "glx_backend.h"

#include "glx.h"
#include "x11_logging.h"

#include "base/platform.h"
#include "main.h"
#include "options.h"
#include "render/compositor.h"
#include "render/gl/gl.h"
#include "render/gl/texture.h"
#include "render/scene.h"
#include "render/x11/compositor.h"
#include "render/x11/overlay_window.h"
#include "screens.h"
#include "xcbutils.h"

#include "win/x11/geo.h"

// kwin libs
#include <kwineffectquickview.h>
#include <kwinglplatform.h>
#include <kwinglutils.h>
#include <kwinxrenderutils.h>
// Qt
#include <QOpenGLContext>
#include <QX11Info>
// system
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <deque>
#include <stdexcept>

#if HAVE_DL_LIBRARY
#include <dlfcn.h>
#endif

#ifndef XCB_GLX_BUFFER_SWAP_COMPLETE
#define XCB_GLX_BUFFER_SWAP_COMPLETE 1
typedef struct xcb_glx_buffer_swap_complete_event_t {
    uint8_t response_type;       /**<  */
    uint8_t pad0;                /**<  */
    uint16_t sequence;           /**<  */
    uint16_t event_type;         /**<  */
    uint8_t pad1[2];             /**<  */
    xcb_glx_drawable_t drawable; /**<  */
    uint32_t ust_hi;             /**<  */
    uint32_t ust_lo;             /**<  */
    uint32_t msc_hi;             /**<  */
    uint32_t msc_lo;             /**<  */
    uint32_t sbc;                /**<  */
} xcb_glx_buffer_swap_complete_event_t;
#endif

#include <memory>
#include <tuple>

namespace KWin::render::backend::x11
{

swap_event_filter::swap_event_filter(xcb_drawable_t drawable, xcb_glx_drawable_t glxDrawable)
    : base::x11::event_filter(Xcb::Extensions::self()->glxEventBase()
                              + XCB_GLX_BUFFER_SWAP_COMPLETE)
    , m_drawable(drawable)
    , m_glxDrawable(glxDrawable)
{
}

bool swap_event_filter::event(xcb_generic_event_t* event)
{
    xcb_glx_buffer_swap_complete_event_t* ev
        = reinterpret_cast<xcb_glx_buffer_swap_complete_event_t*>(event);

    // The drawable field is the X drawable when the event was synthesized
    // by a WireToEvent handler, and the GLX drawable when the event was
    // received over the wire
    if (ev->drawable == m_drawable || ev->drawable == m_glxDrawable) {
        render::compositor::self()->bufferSwapComplete();
        return true;
    }

    return false;
}

glXSwapIntervalMESA_func glXSwapIntervalMESA;

typedef void (*glXFuncPtr)();

static glXFuncPtr getProcAddress(const char* name)
{
    glXFuncPtr ret = nullptr;
#if HAVE_EPOXY_GLX
    ret = glXGetProcAddress((const GLubyte*)name);
#endif
#if HAVE_DL_LIBRARY
    if (ret == nullptr)
        ret = (glXFuncPtr)dlsym(RTLD_DEFAULT, name);
#endif
    return ret;
}

glx_backend::glx_backend(Display* display, render::compositor& compositor)
    : gl::backend()
    , overlay_window{std::make_unique<render::x11::overlay_window>()}
    , ctx(nullptr)
    , m_bufferAge(0)
    , m_x11Display(display)
    , compositor{compositor}
{
    auto x11_compositor = dynamic_cast<render::x11::compositor*>(&compositor);
    assert(x11_compositor);
    x11_compositor->overlay_window = overlay_window.get();

    // Force initialization of GLX integration in the Qt's xcb backend
    // to make it call XESetWireToEvent callbacks, which is required
    // by Mesa when using DRI2.
    QOpenGLContext::supportsThreadedOpenGL();

    check_glx_version();
    set_glx_extensions(*this);

    // resolve glXSwapIntervalMESA if available
    if (hasExtension(QByteArrayLiteral("GLX_MESA_swap_control"))) {
        glXSwapIntervalMESA = (glXSwapIntervalMESA_func)getProcAddress("glXSwapIntervalMESA");
    } else {
        glXSwapIntervalMESA = nullptr;
    }

    populate_visual_depth_hash_table(m_visualDepthHash);

    if (!init_glx_buffer(*this)) {
        throw std::runtime_error("Could not initialize the buffer");
    }

    ctx = create_glx_context(*this);
    if (!ctx) {
        throw std::runtime_error("Could not initialize rendering context");
    }

    gl::init_gl(GlxPlatformInterface, getProcAddress);

    // Check whether certain features are supported
    m_haveMESACopySubBuffer = hasExtension(QByteArrayLiteral("GLX_MESA_copy_sub_buffer"));
    m_haveMESASwapControl = hasExtension(QByteArrayLiteral("GLX_MESA_swap_control"));
    m_haveEXTSwapControl = hasExtension(QByteArrayLiteral("GLX_EXT_swap_control"));

    // Allow to disable Intel swap event with env variable. There were problems in the past.
    // See BUG 342582.
    if (hasExtension(QByteArrayLiteral("GLX_INTEL_swap_event"))
        && qgetenv("KWIN_USE_INTEL_SWAP_EVENT") != QByteArrayLiteral("0")) {
        swap_filter = std::make_unique<swap_event_filter>(window, glxWindow);
        glXSelectEvent(display, glxWindow, GLX_BUFFER_SWAP_COMPLETE_INTEL_MASK);
    }

    setSupportsBufferAge(false);

    if (hasExtension(QByteArrayLiteral("GLX_EXT_buffer_age"))) {
        const QByteArray useBufferAge = qgetenv("KWIN_USE_BUFFER_AGE");

        if (useBufferAge != "0")
            setSupportsBufferAge(true);
    }

    if (m_haveEXTSwapControl) {
        glXSwapIntervalEXT(display, glxWindow, 1);
    } else if (m_haveMESASwapControl) {
        glXSwapIntervalMESA(1);
    } else {
        qCWarning(KWIN_X11) << "NO VSYNC! glSwapInterval is not supported";
    }

    if (GLPlatform::instance()->isVirtualBox()) {
        // VirtualBox does not support glxQueryDrawable
        // this should actually be in kwinglutils_funcs, but QueryDrawable seems not to be provided
        // by an extension and the GLPlatform has not been initialized at the moment when initGLX()
        // is called.
        glXQueryDrawable = nullptr;
    }

    setIsDirectRendering(bool(glXIsDirect(display, ctx)));

    qCDebug(KWIN_X11) << "Direct rendering:" << isDirectRendering();
}

glx_backend::~glx_backend()
{
    // TODO: cleanup in error case
    // do cleanup after initBuffer()
    cleanupGL();
    doneCurrent();
    EffectQuickView::setShareContext(nullptr);

    if (ctx)
        glXDestroyContext(display(), ctx);

    if (glxWindow)
        glXDestroyWindow(display(), glxWindow);

    if (window)
        XDestroyWindow(display(), window);

    for (auto& [key, val] : m_fbconfigHash) {
        delete val;
    }
    m_fbconfigHash.clear();

    overlay_window->destroy();
}

void glx_backend::check_glx_version()
{
    int major, minor;
    glXQueryVersion(display(), &major, &minor);
    if (kVersionNumber(major, minor) < kVersionNumber(1, 3)) {
        throw std::runtime_error("Requires at least GLX 1.3");
    }
}

int glx_backend::visualDepth(xcb_visualid_t visual) const
{
    auto it = m_visualDepthHash.find(visual);
    return it == m_visualDepthHash.end() ? 0 : it->second;
}

static inline int bitCount(uint32_t mask)
{
#if defined(__GNUC__)
    return __builtin_popcount(mask);
#else
    int count = 0;

    while (mask) {
        count += (mask & 1);
        mask >>= 1;
    }

    return count;
#endif
}

fb_config_info* glx_backend::infoForVisual(xcb_visualid_t visual)
{
    auto it = m_fbconfigHash.find(visual);
    if (it != m_fbconfigHash.end()) {
        return it->second;
    }

    auto info = new fb_config_info;
    m_fbconfigHash.insert({visual, info});
    info->fbconfig = nullptr;
    info->bind_texture_format = 0;
    info->texture_targets = 0;
    info->y_inverted = 0;
    info->mipmap = 0;

    const xcb_render_pictformat_t format = XRenderUtils::findPictFormat(visual);
    const xcb_render_directformat_t* direct = XRenderUtils::findPictFormatInfo(format);

    if (!direct) {
        qCCritical(KWIN_X11).nospace()
            << "Could not find a picture format for visual 0x" << hex << visual;
        return info;
    }

    const int red_bits = bitCount(direct->red_mask);
    const int green_bits = bitCount(direct->green_mask);
    const int blue_bits = bitCount(direct->blue_mask);
    const int alpha_bits = bitCount(direct->alpha_mask);

    const int depth = visualDepth(visual);

    const auto rgb_sizes = std::tie(red_bits, green_bits, blue_bits);

    const int attribs[]
        = {GLX_RENDER_TYPE,
           GLX_RGBA_BIT,
           GLX_DRAWABLE_TYPE,
           GLX_WINDOW_BIT | GLX_PIXMAP_BIT,
           GLX_X_VISUAL_TYPE,
           GLX_TRUE_COLOR,
           GLX_X_RENDERABLE,
           True,
           GLX_CONFIG_CAVEAT,
           int(GLX_DONT_CARE), // The ARGB32 visual is marked non-conformant in Catalyst
           GLX_FRAMEBUFFER_SRGB_CAPABLE_EXT,
           int(GLX_DONT_CARE), // The ARGB32 visual is marked sRGB capable in mesa/i965
           GLX_BUFFER_SIZE,
           red_bits + green_bits + blue_bits + alpha_bits,
           GLX_RED_SIZE,
           red_bits,
           GLX_GREEN_SIZE,
           green_bits,
           GLX_BLUE_SIZE,
           blue_bits,
           GLX_ALPHA_SIZE,
           alpha_bits,
           GLX_STENCIL_SIZE,
           0,
           GLX_DEPTH_SIZE,
           0,
           0};

    int count = 0;
    GLXFBConfig* configs = glXChooseFBConfig(display(), DefaultScreen(display()), attribs, &count);

    if (count < 1) {
        qCCritical(KWIN_X11).nospace()
            << "Could not find a framebuffer configuration for visual 0x" << hex << visual;
        return info;
    }

    struct FBConfig {
        GLXFBConfig config;
        int depth;
        int stencil;
        int format;
    };

    std::deque<FBConfig> candidates;

    for (int i = 0; i < count; i++) {
        int red, green, blue;
        glXGetFBConfigAttrib(display(), configs[i], GLX_RED_SIZE, &red);
        glXGetFBConfigAttrib(display(), configs[i], GLX_GREEN_SIZE, &green);
        glXGetFBConfigAttrib(display(), configs[i], GLX_BLUE_SIZE, &blue);

        if (std::tie(red, green, blue) != rgb_sizes)
            continue;

        xcb_visualid_t visual;
        glXGetFBConfigAttrib(display(), configs[i], GLX_VISUAL_ID, (int*)&visual);

        if (visualDepth(visual) != depth)
            continue;

        int bind_rgb, bind_rgba;
        glXGetFBConfigAttrib(display(), configs[i], GLX_BIND_TO_TEXTURE_RGBA_EXT, &bind_rgba);
        glXGetFBConfigAttrib(display(), configs[i], GLX_BIND_TO_TEXTURE_RGB_EXT, &bind_rgb);

        if (!bind_rgb && !bind_rgba)
            continue;

        int depth, stencil;
        glXGetFBConfigAttrib(display(), configs[i], GLX_DEPTH_SIZE, &depth);
        glXGetFBConfigAttrib(display(), configs[i], GLX_STENCIL_SIZE, &stencil);

        int texture_format;
        if (alpha_bits)
            texture_format = bind_rgba ? GLX_TEXTURE_FORMAT_RGBA_EXT : GLX_TEXTURE_FORMAT_RGB_EXT;
        else
            texture_format = bind_rgb ? GLX_TEXTURE_FORMAT_RGB_EXT : GLX_TEXTURE_FORMAT_RGBA_EXT;

        candidates.emplace_back(FBConfig{configs[i], depth, stencil, texture_format});
    }

    if (count > 0)
        XFree(configs);

    std::stable_sort(
        candidates.begin(), candidates.end(), [](const FBConfig& left, const FBConfig& right) {
            if (left.depth < right.depth)
                return true;

            if (left.stencil < right.stencil)
                return true;

            return false;
        });

    if (candidates.size() > 0) {
        const FBConfig& candidate = candidates.front();

        int y_inverted, texture_targets;
        glXGetFBConfigAttrib(
            display(), candidate.config, GLX_BIND_TO_TEXTURE_TARGETS_EXT, &texture_targets);
        glXGetFBConfigAttrib(display(), candidate.config, GLX_Y_INVERTED_EXT, &y_inverted);

        info->fbconfig = candidate.config;
        info->bind_texture_format = candidate.format;
        info->texture_targets = texture_targets;
        info->y_inverted = y_inverted;
        info->mipmap = 0;
    }

    if (info->fbconfig) {
        int fbc_id = 0;
        int visual_id = 0;

        glXGetFBConfigAttrib(display(), info->fbconfig, GLX_FBCONFIG_ID, &fbc_id);
        glXGetFBConfigAttrib(display(), info->fbconfig, GLX_VISUAL_ID, &visual_id);

        qCDebug(KWIN_X11).nospace()
            << "Using FBConfig 0x" << hex << fbc_id << " for visual 0x" << hex << visual_id;
    }

    return info;
}

void glx_backend::present()
{
    if (lastDamage().isEmpty())
        return;

    auto const& screenSize = kwinApp()->get_base().screens.size();
    const QRegion displayRegion(0, 0, screenSize.width(), screenSize.height());
    const bool canSwapBuffers = supportsBufferAge() || (lastDamage() == displayRegion);
    m_needsCompositeTimerStart = true;

    if (canSwapBuffers) {
        if (supportsSwapEvents()) {
            m_needsCompositeTimerStart = false;
            compositor.aboutToSwapBuffers();
        }

        glXSwapBuffers(display(), glxWindow);

        if (supportsBufferAge()) {
            glXQueryDrawable(display(), glxWindow, GLX_BACK_BUFFER_AGE_EXT, (GLuint*)&m_bufferAge);
        }
    } else if (m_haveMESACopySubBuffer) {
        for (const QRect& r : lastDamage()) {
            // convert to OpenGL coordinates
            int y = screenSize.height() - r.y() - r.height();
            glXCopySubBufferMESA(display(), glxWindow, r.x(), y, r.width(), r.height());
        }
    } else {
        // Copy Pixels (horribly slow on Mesa).
        glDrawBuffer(GL_FRONT);
        copyPixels(lastDamage());
        glDrawBuffer(GL_BACK);
    }

    setLastDamage(QRegion());
    if (!supportsBufferAge()) {
        glXWaitGL();
        XFlush(display());
    }
}

void glx_backend::screenGeometryChanged(const QSize& size)
{
    overlay_window->resize(size);
    doneCurrent();

    XMoveResizeWindow(display(), window, 0, 0, size.width(), size.height());
    overlay_window->setup(window);
    Xcb::sync();

    makeCurrent();
    glViewport(0, 0, size.width(), size.height());

    // The back buffer contents are now undefined
    m_bufferAge = 0;
}

gl::texture_private* glx_backend::createBackendTexture(gl::texture* texture)
{
    return new GlxTexture(texture, this);
}

QRegion glx_backend::prepareRenderingFrame()
{
    QRegion repaint;

    if (supportsBufferAge())
        repaint = accumulatedDamageHistory(m_bufferAge);

    startRenderTimer();

    return repaint;
}

void glx_backend::endRenderingFrame(const QRegion& renderedRegion, const QRegion& damagedRegion)
{
    if (damagedRegion.isEmpty()) {
        setLastDamage(QRegion());

        // If the damaged region of a window is fully occluded, the only
        // rendering done, if any, will have been to repair a reused back
        // buffer, making it identical to the front buffer.
        //
        // In this case we won't post the back buffer. Instead we'll just
        // set the buffer age to 1, so the repaired regions won't be
        // rendered again in the next frame.
        if (!renderedRegion.isEmpty())
            glFlush();

        m_bufferAge = 1;
        return;
    }

    setLastDamage(renderedRegion);
    present();

    // Show the window only after the first pass, since that pass may take long.
    if (overlay_window->window()) {
        overlay_window->show();
    }

    // Save the damaged region to history
    if (supportsBufferAge())
        addToDamageHistory(damagedRegion);
}

bool glx_backend::makeCurrent()
{
    if (QOpenGLContext* context = QOpenGLContext::currentContext()) {
        // Workaround to tell Qt that no QOpenGLContext is current
        context->doneCurrent();
    }
    const bool current = glXMakeCurrent(display(), glxWindow, ctx);
    return current;
}

void glx_backend::doneCurrent()
{
    glXMakeCurrent(display(), None, nullptr);
}

bool glx_backend::supportsSwapEvents() const
{
    return swap_filter != nullptr;
}

bool glx_backend::hasSwapEvent() const
{
    return !m_needsCompositeTimerStart;
}

/********************************************************
 * GlxTexture
 *******************************************************/
GlxTexture::GlxTexture(gl::texture* texture, glx_backend* backend)
    : gl::texture_private()
    , q(texture)
    , m_backend(backend)
    , m_glxpixmap(None)
{
}

GlxTexture::~GlxTexture()
{
    if (m_glxpixmap != None) {
        if (!options->isGlStrictBinding()) {
            glXReleaseTexImageEXT(display(), m_glxpixmap, GLX_FRONT_LEFT_EXT);
        }
        glXDestroyPixmap(display(), m_glxpixmap);
        m_glxpixmap = None;
    }
}

void GlxTexture::onDamage()
{
    if (options->isGlStrictBinding() && m_glxpixmap) {
        glXReleaseTexImageEXT(display(), m_glxpixmap, GLX_FRONT_LEFT_EXT);
        glXBindTexImageEXT(display(), m_glxpixmap, GLX_FRONT_LEFT_EXT, nullptr);
    }
    GLTexturePrivate::onDamage();
}

bool GlxTexture::loadTexture(xcb_pixmap_t pixmap, const QSize& size, xcb_visualid_t visual)
{
    if (pixmap == XCB_NONE || size.isEmpty() || visual == XCB_NONE)
        return false;

    auto const info = m_backend->infoForVisual(visual);
    if (!info || info->fbconfig == nullptr)
        return false;

    if (info->texture_targets & GLX_TEXTURE_2D_BIT_EXT) {
        m_target = GL_TEXTURE_2D;
        m_scale.setWidth(1.0f / m_size.width());
        m_scale.setHeight(1.0f / m_size.height());
    } else {
        Q_ASSERT(info->texture_targets & GLX_TEXTURE_RECTANGLE_BIT_EXT);

        m_target = GL_TEXTURE_RECTANGLE;
        m_scale.setWidth(1.0f);
        m_scale.setHeight(1.0f);
    }

    const int attrs[] = {GLX_TEXTURE_FORMAT_EXT,
                         info->bind_texture_format,
                         GLX_MIPMAP_TEXTURE_EXT,
                         false,
                         GLX_TEXTURE_TARGET_EXT,
                         m_target == GL_TEXTURE_2D ? GLX_TEXTURE_2D_EXT : GLX_TEXTURE_RECTANGLE_EXT,
                         0};

    m_glxpixmap = glXCreatePixmap(display(), info->fbconfig, pixmap, attrs);
    m_size = size;
    m_yInverted = info->y_inverted ? true : false;
    m_canUseMipmaps = false;

    glGenTextures(1, &m_texture);

    q->setDirty();
    q->setFilter(GL_NEAREST);

    glBindTexture(m_target, m_texture);
    glXBindTexImageEXT(display(), m_glxpixmap, GLX_FRONT_LEFT_EXT, nullptr);

    updateMatrix();
    return true;
}

bool GlxTexture::loadTexture(render::window_pixmap* pixmap)
{
    Toplevel* t = pixmap->toplevel();
    return loadTexture(pixmap->pixmap(), win::render_geometry(t).size(), t->visual());
}

gl::backend* GlxTexture::backend()
{
    return m_backend;
}

}
