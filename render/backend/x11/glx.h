/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "glx_context_attribute_builder.h"
#include "glx_data.h"
#include "glx_fb_config.h"

#include "base/platform.h"
#include "base/x11/xcb/helpers.h"
#include "main.h"
#include "render/gl/gl.h"
#include "render/x11/compositor.h"
#include "win/space.h"
#include "x11_logging.h"

#include <kwineffects/effect_quick_view.h>
#include <kwinglplatform.h>

#include <QOpenGLContext>
#include <QVariant>
#include <QX11Info>
#include <QtPlatformHeaders/QGLXNativeContext>
#include <cassert>
#include <deque>
#include <epoxy/glx.h>
#include <memory>
#include <stdexcept>
#include <vector>
#include <xcb/xproto.h>

#if HAVE_DL_LIBRARY
#include <dlfcn.h>
#endif

namespace KWin::render::backend::x11
{

template<typename Backend>
void set_glx_extensions(Backend& backend)
{
    QByteArray const string
        = (const char*)glXQueryExtensionsString(backend.data.display, QX11Info::appScreen());
    backend.setExtensions(string.split(' '));
}

template<typename Backend>
GLXFBConfig create_glx_fb_config(Backend const& backend)
{
    auto display = backend.data.display;

    int const attribs[] = {GLX_RENDER_TYPE,
                           GLX_RGBA_BIT,
                           GLX_DRAWABLE_TYPE,
                           GLX_WINDOW_BIT,
                           GLX_RED_SIZE,
                           1,
                           GLX_GREEN_SIZE,
                           1,
                           GLX_BLUE_SIZE,
                           1,
                           GLX_ALPHA_SIZE,
                           0,
                           GLX_DEPTH_SIZE,
                           0,
                           GLX_STENCIL_SIZE,
                           0,
                           GLX_CONFIG_CAVEAT,
                           GLX_NONE,
                           GLX_DOUBLEBUFFER,
                           true,
                           0};

    int const attribs_srgb[] = {GLX_RENDER_TYPE,
                                GLX_RGBA_BIT,
                                GLX_DRAWABLE_TYPE,
                                GLX_WINDOW_BIT,
                                GLX_RED_SIZE,
                                1,
                                GLX_GREEN_SIZE,
                                1,
                                GLX_BLUE_SIZE,
                                1,
                                GLX_ALPHA_SIZE,
                                0,
                                GLX_DEPTH_SIZE,
                                0,
                                GLX_STENCIL_SIZE,
                                0,
                                GLX_CONFIG_CAVEAT,
                                GLX_NONE,
                                GLX_DOUBLEBUFFER,
                                true,
                                GLX_FRAMEBUFFER_SRGB_CAPABLE_ARB,
                                true,
                                0};

    // Try to find a double buffered sRGB capable configuration
    int count{0};
    GLXFBConfig* configs{nullptr};

    // Only request sRGB configurations with default depth 24 as it can cause problems with other
    // default depths. See bugs #408594 and #423014.
    if (base::x11::xcb::default_depth(kwinApp()->x11ScreenNumber()) == 24) {
        configs = glXChooseFBConfig(display, DefaultScreen(display), attribs_srgb, &count);
    }

    if (count == 0) {
        // Try to find a double buffered non-sRGB capable configuration
        configs = glXChooseFBConfig(display, DefaultScreen(display), attribs, &count);
    }

    struct FBConfig {
        GLXFBConfig config;
        int depth;
        int stencil;
    };

    std::deque<FBConfig> candidates;

    for (int i = 0; i < count; i++) {
        int depth, stencil;
        glXGetFBConfigAttrib(display, configs[i], GLX_DEPTH_SIZE, &depth);
        glXGetFBConfigAttrib(display, configs[i], GLX_STENCIL_SIZE, &stencil);

        candidates.emplace_back(FBConfig{configs[i], depth, stencil});
    }

    if (count > 0) {
        XFree(configs);
    }

    std::stable_sort(candidates.begin(), candidates.end(), [](auto const& left, auto const& right) {
        if (left.depth < right.depth)
            return true;

        if (left.stencil < right.stencil)
            return true;

        return false;
    });

    GLXFBConfig fbconfig{nullptr};
    if (candidates.size() > 0) {
        fbconfig = candidates.front().config;

        int fbconfig_id, visual_id, red, green, blue, alpha, depth, stencil, srgb;
        glXGetFBConfigAttrib(display, fbconfig, GLX_FBCONFIG_ID, &fbconfig_id);
        glXGetFBConfigAttrib(display, fbconfig, GLX_VISUAL_ID, &visual_id);
        glXGetFBConfigAttrib(display, fbconfig, GLX_RED_SIZE, &red);
        glXGetFBConfigAttrib(display, fbconfig, GLX_GREEN_SIZE, &green);
        glXGetFBConfigAttrib(display, fbconfig, GLX_BLUE_SIZE, &blue);
        glXGetFBConfigAttrib(display, fbconfig, GLX_ALPHA_SIZE, &alpha);
        glXGetFBConfigAttrib(display, fbconfig, GLX_DEPTH_SIZE, &depth);
        glXGetFBConfigAttrib(display, fbconfig, GLX_STENCIL_SIZE, &stencil);
        glXGetFBConfigAttrib(display, fbconfig, GLX_FRAMEBUFFER_SRGB_CAPABLE_ARB, &srgb);

        qCDebug(KWIN_X11,
                "Choosing GLXFBConfig %#x X visual %#x depth %d RGBA %d:%d:%d:%d ZS %d:%d sRGB: %d",
                fbconfig_id,
                visual_id,
                backend.visualDepth(visual_id),
                red,
                green,
                blue,
                alpha,
                depth,
                stencil,
                srgb);
    }

    if (!fbconfig) {
        qCCritical(KWIN_X11) << "Failed to find a usable framebuffer configuration";
    }

    return fbconfig;
}

template<typename Backend>
bool init_glx_buffer(Backend& backend)
{
    backend.data.fbconfig = create_glx_fb_config(backend);
    if (!backend.data.fbconfig) {
        return false;
    }

    if (backend.overlay_window->create()) {
        auto c = connection();

        // Try to create double-buffered window in the overlay
        xcb_visualid_t visual;
        glXGetFBConfigAttrib(
            backend.data.display, backend.data.fbconfig, GLX_VISUAL_ID, (int*)&visual);

        if (!visual) {
            qCCritical(KWIN_X11) << "The GLXFBConfig does not have an associated X visual";
            return false;
        }

        xcb_colormap_t colormap = xcb_generate_id(c);
        xcb_create_colormap(c, false, colormap, rootWindow(), visual);

        auto const& space_size = backend.compositor.platform.base.topology.size;
        backend.window = xcb_generate_id(c);
        xcb_create_window(c,
                          backend.visualDepth(visual),
                          backend.window,
                          backend.overlay_window->window(),
                          0,
                          0,
                          space_size.width(),
                          space_size.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          visual,
                          XCB_CW_COLORMAP,
                          &colormap);

        backend.data.window
            = glXCreateWindow(backend.data.display, backend.data.fbconfig, backend.window, nullptr);
        backend.overlay_window->setup(backend.window);
    } else {
        qCCritical(KWIN_X11) << "Failed to create overlay window";
        return false;
    }

    return true;
}

template<typename Container>
void populate_visual_depth_hash_table(Container& container)
{
    auto setup = xcb_get_setup(connection());

    for (auto screen = xcb_setup_roots_iterator(setup); screen.rem; xcb_screen_next(&screen)) {
        for (auto depth = xcb_screen_allowed_depths_iterator(screen.data); depth.rem;
             xcb_depth_next(&depth)) {
            const int len = xcb_depth_visuals_length(depth.data);
            const xcb_visualtype_t* visuals = xcb_depth_visuals(depth.data);

            for (int i = 0; i < len; i++)
                container.insert({visuals[i].visual_id, depth.data->depth});
        }
    }
}

template<typename Backend>
GLXContext create_glx_context(Backend const& backend)
{
    GLXContext ctx{nullptr};
    auto const direct = true;

    // Use glXCreateContextAttribsARB() when it's available
    if (backend.hasExtension(QByteArrayLiteral("GLX_ARB_create_context"))) {
        auto const have_robustness
            = backend.hasExtension(QByteArrayLiteral("GLX_ARB_create_context_robustness"));
        auto const haveVideoMemoryPurge
            = backend.hasExtension(QByteArrayLiteral("GLX_NV_robustness_video_memory_purge"));

        std::vector<glx_context_attribute_builder> candidates;

        // core
        if (have_robustness) {
            if (haveVideoMemoryPurge) {
                glx_context_attribute_builder purgeMemoryCore;
                purgeMemoryCore.setVersion(3, 1);
                purgeMemoryCore.setRobust(true);
                purgeMemoryCore.setResetOnVideoMemoryPurge(true);
                candidates.emplace_back(std::move(purgeMemoryCore));
            }
            glx_context_attribute_builder robustCore;
            robustCore.setVersion(3, 1);
            robustCore.setRobust(true);
            candidates.emplace_back(std::move(robustCore));
        }
        glx_context_attribute_builder core;
        core.setVersion(3, 1);
        candidates.emplace_back(std::move(core));

        // legacy
        if (have_robustness) {
            if (haveVideoMemoryPurge) {
                glx_context_attribute_builder purgeMemoryLegacy;
                purgeMemoryLegacy.setRobust(true);
                purgeMemoryLegacy.setResetOnVideoMemoryPurge(true);
                candidates.emplace_back(std::move(purgeMemoryLegacy));
            }
            glx_context_attribute_builder robustLegacy;
            robustLegacy.setRobust(true);
            candidates.emplace_back(std::move(robustLegacy));
        }
        glx_context_attribute_builder legacy;
        legacy.setVersion(2, 1);
        candidates.emplace_back(std::move(legacy));

        for (auto& candidate : candidates) {
            auto const attribs = candidate.build();
            ctx = glXCreateContextAttribsARB(
                backend.data.display, backend.data.fbconfig, nullptr, true, attribs.data());
            if (ctx) {
                qCDebug(KWIN_X11) << "Created GLX context with attributes:" << &candidate;
                break;
            }
        }
    }

    if (!ctx) {
        ctx = glXCreateNewContext(
            backend.data.display, backend.data.fbconfig, GLX_RGBA_TYPE, nullptr, direct);
    }

    if (!ctx) {
        qCDebug(KWIN_X11) << "Failed to create an OpenGL context.";
        return nullptr;
    }

    if (!glXMakeCurrent(backend.data.display, backend.data.window, ctx)) {
        qCDebug(KWIN_X11) << "Failed to make the OpenGL context current.";
        glXDestroyContext(backend.data.display, ctx);
        return nullptr;
    }

    auto qtContext = new QOpenGLContext;
    QGLXNativeContext native(ctx, backend.data.display);
    qtContext->setNativeHandle(QVariant::fromValue(native));
    qtContext->create();
    EffectQuickView::setShareContext(std::unique_ptr<QOpenGLContext>(qtContext));

    return ctx;
}

static void check_glx_version(Display* display)
{
    int major, minor;
    glXQueryVersion(display, &major, &minor);
    if (kVersionNumber(major, minor) < kVersionNumber(1, 3)) {
        throw std::runtime_error("Requires at least GLX 1.3");
    }
}

typedef void (*glXFuncPtr)();

static glXFuncPtr getProcAddress(const char* name)
{
    glXFuncPtr ret = nullptr;
#if HAVE_EPOXY_GLX
    ret = glXGetProcAddress((const GLubyte*)name);
#endif
#if HAVE_DL_LIBRARY
    if (ret == nullptr) {
        ret = (glXFuncPtr)dlsym(RTLD_DEFAULT, name);
    }
#endif
    return ret;
}

template<typename Backend>
void start_glx_backend(Display* display, render::compositor& compositor, Backend& backend)
{
    backend.data.display = display;
    backend.overlay_window = std::make_unique<render::x11::overlay_window>();

    auto x11_compositor = dynamic_cast<render::x11::compositor*>(&compositor);
    assert(x11_compositor);
    x11_compositor->overlay_window = backend.overlay_window.get();

    // Force initialization of GLX integration in the Qt's xcb backend
    // to make it call XESetWireToEvent callbacks, which is required
    // by Mesa when using DRI2.
    QOpenGLContext::supportsThreadedOpenGL();

    check_glx_version(display);
    set_glx_extensions(backend);

    if (backend.hasExtension(QByteArrayLiteral("GLX_MESA_swap_control"))) {
        backend.data.swap_interval_mesa = reinterpret_cast<glx_data::swap_interval_mesa_func>(
            getProcAddress("glXSwapIntervalMESA"));
    }

    populate_visual_depth_hash_table(backend.visual_depth_hash);

    if (!init_glx_buffer(backend)) {
        throw std::runtime_error("Could not initialize the buffer");
    }

    backend.data.context = create_glx_context(backend);
    if (!backend.data.context) {
        throw std::runtime_error("Could not initialize rendering context");
    }

    gl::init_gl(GlxPlatformInterface, getProcAddress);

    // Check whether certain features are supported
    backend.data.extensions.mesa_copy_sub_buffer
        = backend.hasExtension(QByteArrayLiteral("GLX_MESA_copy_sub_buffer"));
    backend.data.extensions.mesa_swap_control
        = backend.hasExtension(QByteArrayLiteral("GLX_MESA_swap_control"));
    backend.data.extensions.ext_swap_control
        = backend.hasExtension(QByteArrayLiteral("GLX_EXT_swap_control"));

    // Allow to disable Intel swap event with env variable. There were problems in the past.
    // See BUG 342582.
    if (backend.hasExtension(QByteArrayLiteral("GLX_INTEL_swap_event"))
        && qgetenv("KWIN_USE_INTEL_SWAP_EVENT") != QByteArrayLiteral("0")) {
        backend.data.swap_filter
            = std::make_unique<swap_event_filter>(backend.window, backend.data.window);
        glXSelectEvent(display, backend.data.window, GLX_BUFFER_SWAP_COMPLETE_INTEL_MASK);
    }

    backend.setSupportsBufferAge(false);

    if (backend.hasExtension(QByteArrayLiteral("GLX_EXT_buffer_age"))) {
        const QByteArray useBufferAge = qgetenv("KWIN_USE_BUFFER_AGE");

        if (useBufferAge != "0")
            backend.setSupportsBufferAge(true);
    }

    if (backend.data.extensions.ext_swap_control) {
        glXSwapIntervalEXT(display, backend.data.window, 1);
    } else if (backend.data.extensions.mesa_swap_control) {
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

    backend.setIsDirectRendering(bool(glXIsDirect(display, backend.data.context)));
    qCDebug(KWIN_X11) << "Direct rendering:" << backend.isDirectRendering();
}

template<typename Backend>
void tear_down_glx_backend(Backend& backend)
{
    // TODO: cleanup in error case
    // do cleanup after initBuffer()
    cleanupGL();
    backend.doneCurrent();
    EffectQuickView::setShareContext(nullptr);

    if (backend.data.context) {
        glXDestroyContext(backend.data.display, backend.data.context);
    }

    if (backend.data.window) {
        glXDestroyWindow(backend.data.display, backend.data.window);
    }

    if (backend.window) {
        XDestroyWindow(backend.data.display, backend.window);
    }

    for (auto& [key, val] : backend.fb_configs) {
        delete val;
    }
    backend.fb_configs.clear();

    backend.overlay_window->destroy();
    backend.overlay_window.reset();
    backend.data = {};
}

}
