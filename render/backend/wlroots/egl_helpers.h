/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "surface.h"

#include <wayland_logging.h>

#include <epoxy/egl.h>
#include <memory>
#include <stdexcept>

namespace KWin::render::backend::wlroots
{

class egl_gbm
{
public:
    EGLDisplay egl_display{EGL_NO_DISPLAY};
    gbm_device* gbm_dev;

    egl_gbm(EGLDisplay egl_display, gbm_device* gbm_dev)
        : egl_display{egl_display}
        , gbm_dev{gbm_dev}
    {
        assert(egl_display != EGL_NO_DISPLAY);
        assert(gbm_dev);
    }
    ~egl_gbm()
    {
        // TODO(romangg): The EGLDisplay should be destroyed here too.
        gbm_device_destroy(gbm_dev);
    }

    egl_gbm(egl_gbm const&) = delete;
    egl_gbm& operator=(egl_gbm const&) = delete;
    egl_gbm(egl_gbm&&) noexcept = default;
    egl_gbm& operator=(egl_gbm&&) noexcept = default;
};

template<typename Egl>
EGLDisplay get_egl_headless(Egl const& egl)
{
    auto const has_mesa_headless
        = egl.hasClientExtension(QByteArrayLiteral("EGL_MESA_platform_surfaceless"));

    if (!has_mesa_headless) {
        throw std::runtime_error("Missing EGL_MESA_platform_surfaceless extension");
    }

    return eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
}

template<typename Platform, typename Egl>
std::unique_ptr<egl_gbm> get_egl_gbm(Platform const& platform, Egl const& egl)
{
    auto const has_mesa_gbm = egl.hasClientExtension(QByteArrayLiteral("EGL_MESA_platform_gbm"));
    auto const has_khr_gbm = egl.hasClientExtension(QByteArrayLiteral("EGL_KHR_platform_gbm"));

    if (!egl.hasClientExtension(QByteArrayLiteral("EGL_EXT_platform_base"))
        || (!has_mesa_gbm && !has_khr_gbm)) {
        throw std::runtime_error(
            "Missing one or more extensions between EGL_EXT_platform_base, EGL_MESA_platform_gbm, "
            "EGL_KHR_platform_gbm");
    }

#if HAVE_WLR_OUTPUT_INIT_RENDER
    auto renderer = platform.renderer;
#else
    auto renderer = wlr_backend_get_renderer(platform.base.backend);
#endif

    auto gbm_dev = gbm_create_device(wlr_renderer_get_drm_fd(renderer));
    if (!gbm_dev) {
        throw std::runtime_error("Could not create gbm device");
    }

    auto const egl_platform = has_mesa_gbm ? EGL_PLATFORM_GBM_MESA : EGL_PLATFORM_GBM_KHR;
    auto egl_display = eglGetPlatformDisplayEXT(egl_platform, gbm_dev, nullptr);
    if (egl_display == EGL_NO_DISPLAY) {
        gbm_device_destroy(gbm_dev);
        return nullptr;
    }

    return std::make_unique<egl_gbm>(egl_display, gbm_dev);
}

template<typename Egl>
gbm_surface* create_gbm_surface(Egl const& egl, QSize const& size)
{
    auto surface = gbm_surface_create(egl.gbm->gbm_dev,
                                      size.width(),
                                      size.height(),
                                      GBM_FORMAT_ARGB8888,
                                      GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!surface) {
        qCCritical(KWIN_WL) << "Creating GBM surface failed";
    }
    return surface;
}

template<typename Egl>
EGLSurface create_egl_surface(Egl const& egl, gbm_surface* gbm_surf)
{
    auto egl_surf = eglCreatePlatformWindowSurfaceEXT(
        egl.eglDisplay(), egl.config(), reinterpret_cast<void*>(gbm_surf), nullptr);

    if (egl_surf == EGL_NO_SURFACE) {
        qCCritical(KWIN_WL) << "Creating EGL surface failed";
    }

    return egl_surf;
}

template<typename Egl>
std::unique_ptr<surface> create_surface(Egl const& egl, QSize const& size)
{
    auto gbm_surf = create_gbm_surface(egl, size);
    if (!gbm_surf) {
        return nullptr;
    }
    auto egl_surf = create_egl_surface(egl, gbm_surf);
    if (!egl_surf) {
        return nullptr;
    }
    return std::make_unique<surface>(gbm_surf, egl_surf, egl.eglDisplay(), size);
}

template<typename Egl>
std::unique_ptr<surface> create_headless_surface(Egl const& egl, QSize const& size)
{
    EGLint const attribs[] = {
        EGL_HEIGHT,
        size.height(),
        EGL_WIDTH,
        size.width(),
        EGL_NONE,
    };
    auto egl_surf = eglCreatePbufferSurface(egl.eglDisplay(), egl.config(), attribs);
    if (!egl_surf) {
        return nullptr;
    }
    return std::make_unique<surface>(nullptr, egl_surf, egl.eglDisplay(), size);
}

template<typename EglBackend>
bool init_buffer_configs(EglBackend* egl_back)
{
    EGLint const config_attribs[] = {
        EGL_SURFACE_TYPE,
        egl_back->headless ? EGL_PBUFFER_BIT : EGL_WINDOW_BIT,
        EGL_RED_SIZE,
        1,
        EGL_GREEN_SIZE,
        1,
        EGL_BLUE_SIZE,
        1,
        EGL_ALPHA_SIZE,
        0,
        EGL_RENDERABLE_TYPE,
        egl_back->isOpenGLES() ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_BIT,
        EGL_CONFIG_CAVEAT,
        EGL_NONE,
        EGL_NONE,
    };

    EGLint count;
    EGLConfig configs[1024];
    auto display = egl_back->eglDisplay();

    if (!eglChooseConfig(
            display, config_attribs, configs, sizeof(configs) / sizeof(EGLConfig), &count)) {
        qCCritical(KWIN_WL) << "choose config failed";
        return false;
    }

    qCDebug(KWIN_WL) << "EGL buffer configs count:" << count;

    if (egl_back->headless) {
        if (count == 0) {
            qCCritical(KWIN_WL) << "No suitable config for headless backend found.";
            return false;
        }
        egl_back->setConfig(configs[0]);
        return true;
    }

    for (EGLint i = 0; i < count; i++) {
        EGLint gbmFormat;
        // Query some configuration parameters, to show in debug log.
        eglGetConfigAttrib(display, configs[i], EGL_NATIVE_VISUAL_ID, &gbmFormat);

        if (KWIN_WL().isDebugEnabled()) {
            // GBM formats are declared as FOURCC code (integer from ASCII chars, so use this fact).
            char gbmFormatStr[sizeof(EGLint) + 1] = {0};
            memcpy(gbmFormatStr, &gbmFormat, sizeof(EGLint));

            // Query number of bits for color channel.
            EGLint blueSize, redSize, greenSize, alphaSize;
            eglGetConfigAttrib(display, configs[i], EGL_RED_SIZE, &redSize);
            eglGetConfigAttrib(display, configs[i], EGL_GREEN_SIZE, &greenSize);
            eglGetConfigAttrib(display, configs[i], EGL_BLUE_SIZE, &blueSize);
            eglGetConfigAttrib(display, configs[i], EGL_ALPHA_SIZE, &alphaSize);
            qCDebug(KWIN_WL) << "  EGL config #" << i << " has GBM FOURCC format:" << gbmFormatStr
                             << "; color sizes (RGBA order):" << redSize << greenSize << blueSize
                             << alphaSize;
        }

        if ((gbmFormat == GBM_FORMAT_XRGB8888) || (gbmFormat == GBM_FORMAT_ARGB8888)) {
            egl_back->setConfig(configs[i]);
            return true;
        }
    }

    qCCritical(KWIN_WL) << "Choosing EGL config did not return a suitable config. There were"
                        << count << "configs.";
    return false;
}

template<typename EglBackend>
bool make_current(EGLSurface surface, EglBackend& egl_back)
{
    if (surface == EGL_NO_SURFACE) {
        qCCritical(KWIN_WL) << "Make Context Current failed: no surface";
        return false;
    }
    if (eglMakeCurrent(egl_back.eglDisplay(), surface, surface, egl_back.context()) == EGL_FALSE) {
        qCCritical(KWIN_WL) << "Make Context Current failed:" << eglGetError();
        return false;
    }
    return true;
}

}
