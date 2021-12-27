/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "glx_context_attribute_builder.h"

#include "base/platform.h"
#include "x11_logging.h"
#include "xcbutils.h"
#include <kwineffectquickview.h>
#include <kwinglplatform.h>

#include <QOpenGLContext>
#include <QX11Info>
#include <QtPlatformHeaders/QGLXNativeContext>
#include <deque>
#include <epoxy/glx.h>
#include <memory>
#include <vector>
#include <xcb/xproto.h>

namespace KWin::render::backend::x11
{

template<typename Backend>
void set_glx_extensions(Backend& backend)
{
    QByteArray const string
        = (const char*)glXQueryExtensionsString(backend.display(), QX11Info::appScreen());
    backend.setExtensions(string.split(' '));
}

template<typename Backend>
GLXFBConfig create_glx_fb_config(Backend const& backend)
{
    auto display = backend.display();

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

    auto on_llvmpipe = false;

    // Note that we cannot use GLPlatform::driver() here, because it has not been initialized at
    // this point
    if (backend.hasExtension(QByteArrayLiteral("GLX_MESA_query_renderer"))) {
        const QByteArray device = glXQueryRendererStringMESA(
            display, DefaultScreen(display), 0, GLX_RENDERER_DEVICE_ID_MESA);
        if (device.contains(QByteArrayLiteral("llvmpipe"))) {
            on_llvmpipe = true;
        }
    }

    // Try to find a double buffered sRGB capable configuration
    int count{0};
    GLXFBConfig* configs{nullptr};

    // Don't request an sRGB configuration with LLVMpipe when the default depth is 16. See bug
    // #408594.
    if (!on_llvmpipe || Xcb::defaultDepth() > 16) {
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

    GLXFBConfig fbconfig;
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
    backend.fbconfig = create_glx_fb_config(backend);
    if (!backend.fbconfig) {
        return false;
    }

    if (backend.overlay_window->create()) {
        auto c = connection();

        // Try to create double-buffered window in the overlay
        xcb_visualid_t visual;
        glXGetFBConfigAttrib(backend.display(), backend.fbconfig, GLX_VISUAL_ID, (int*)&visual);

        if (!visual) {
            qCCritical(KWIN_X11) << "The GLXFBConfig does not have an associated X visual";
            return false;
        }

        xcb_colormap_t colormap = xcb_generate_id(c);
        xcb_create_colormap(c, false, colormap, rootWindow(), visual);

        auto const& size = kwinApp()->get_base().screens.size();

        backend.window = xcb_generate_id(c);
        xcb_create_window(c,
                          backend.visualDepth(visual),
                          backend.window,
                          backend.overlay_window->window(),
                          0,
                          0,
                          size.width(),
                          size.height(),
                          0,
                          XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          visual,
                          XCB_CW_COLORMAP,
                          &colormap);

        backend.glxWindow
            = glXCreateWindow(backend.display(), backend.fbconfig, backend.window, nullptr);
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
                backend.display(), backend.fbconfig, nullptr, true, attribs.data());
            if (ctx) {
                qCDebug(KWIN_X11) << "Created GLX context with attributes:" << &candidate;
                break;
            }
        }
    }

    if (!ctx) {
        ctx = glXCreateNewContext(
            backend.display(), backend.fbconfig, GLX_RGBA_TYPE, nullptr, direct);
    }

    if (!ctx) {
        qCDebug(KWIN_X11) << "Failed to create an OpenGL context.";
        return nullptr;
    }

    if (!glXMakeCurrent(backend.display(), backend.glxWindow, ctx)) {
        qCDebug(KWIN_X11) << "Failed to make the OpenGL context current.";
        glXDestroyContext(backend.display(), ctx);
        return nullptr;
    }

    auto qtContext = new QOpenGLContext;
    QGLXNativeContext native(ctx, backend.display());
    qtContext->setNativeHandle(QVariant::fromValue(native));
    qtContext->create();
    EffectQuickView::setShareContext(std::unique_ptr<QOpenGLContext>(qtContext));

    return ctx;
}

}
