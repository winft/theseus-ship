/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "egl_x11_backend.h"

#include "logging.h"
#include "options.h"
#include "screens.h"
#include "x11windowed_backend.h"
#include "xcbutils.h"

#include <kwinglplatform.h>

namespace KWin
{

EglX11Backend::EglX11Backend(X11WindowedBackend *backend)
    : AbstractEglBackend()
    , m_backend(backend)
{
    setIsDirectRendering(true);
}

EglX11Backend::~EglX11Backend()
{
    cleanup();
}

void EglX11Backend::cleanupSurfaces()
{
    for (auto it = m_surfaces.begin(); it != m_surfaces.end(); ++it) {
        eglDestroySurface(eglDisplay(), *it);
    }
}

void EglX11Backend::init()
{
    qputenv("EGL_PLATFORM", "x11");
    if (!initRenderingContext()) {
        setFailed(QStringLiteral("Could not initialize rendering context"));
        return;
    }

    initKWinGL();
    if (!hasExtension(QByteArrayLiteral("EGL_KHR_image")) &&
        (!hasExtension(QByteArrayLiteral("EGL_KHR_image_base")) ||
         !hasExtension(QByteArrayLiteral("EGL_KHR_image_pixmap")))) {
        setFailed(QStringLiteral("Required support for binding pixmaps to EGLImages not found, disabling compositing"));
        return;
    }
    if (!hasGLExtension(QByteArrayLiteral("GL_OES_EGL_image"))) {
        setFailed(QStringLiteral("Required extension GL_OES_EGL_image not found, disabling compositing"));
        return;
    }

    // check for EGL_NV_post_sub_buffer and whether it can be used on the surface
    if (hasExtension(QByteArrayLiteral("EGL_NV_post_sub_buffer"))) {
        if (eglQuerySurface(eglDisplay(), surface(), EGL_POST_SUB_BUFFER_SUPPORTED_NV, &m_surfaceHasSubPost) == EGL_FALSE) {
            EGLint error = eglGetError();
            if (error != EGL_SUCCESS && error != EGL_BAD_ATTRIBUTE) {
                setFailed(QStringLiteral("query surface failed"));
                return;
            } else {
                m_surfaceHasSubPost = EGL_FALSE;
            }
        }
    }

    if (m_surfaceHasSubPost) {
        qCDebug(KWIN_X11WINDOWED) << "EGL implementation and surface support eglPostSubBufferNV, let's use it";

        // check if swap interval 1 is supported
        EGLint val;
        eglGetConfigAttrib(eglDisplay(), config(), EGL_MAX_SWAP_INTERVAL, &val);
        if (val >= 1) {
            if (eglSwapInterval(eglDisplay(), 1)) {
                qCDebug(KWIN_X11WINDOWED) << "Enabled v-sync";
            }
        } else {
            qCWarning(KWIN_X11WINDOWED) << "Cannot enable v-sync as max. swap interval is" << val;
        }

    } else {
        /* In the GLX backend, we fall back to using glCopyPixels if we have no extension providing support for partial screen updates.
         * However, that does not work in EGL - glCopyPixels with glDrawBuffer(GL_FRONT); does nothing.
         * Hence we need EGL to preserve the backbuffer for us, so that we can draw the partial updates on it and call
         * eglSwapBuffers() for each frame. eglSwapBuffers() then does the copy (no page flip possible in this mode),
         * which means it is slow and not synced to the v-blank. */
        qCWarning(KWIN_X11WINDOWED) << "eglPostSubBufferNV not supported, have to enable buffer preservation - which breaks v-sync and performance";
        eglSurfaceAttrib(eglDisplay(), surface(), EGL_SWAP_BEHAVIOR, EGL_BUFFER_PRESERVED);
    }

    initWayland();
}

bool EglX11Backend::initRenderingContext()
{
    initClientExtensions();
    EGLDisplay dpy = kwinApp()->platform()->sceneEglDisplay();

    // Use eglGetPlatformDisplayEXT() to get the display pointer
    // if the implementation supports it.
    if (dpy == EGL_NO_DISPLAY) {
        m_havePlatformBase = hasClientExtension(QByteArrayLiteral("EGL_EXT_platform_base"));
        if (m_havePlatformBase) {
            // Make sure that the X11 platform is supported
            if (!hasClientExtension(QByteArrayLiteral("EGL_EXT_platform_x11")) &&
                !hasClientExtension(QByteArrayLiteral("EGL_KHR_platform_x11"))) {
                qCWarning(KWIN_X11WINDOWED) << "EGL_EXT_platform_base is supported, but neither EGL_EXT_platform_x11 nor EGL_KHR_platform_x11 is supported."
                                     << "Cannot create EGLDisplay on X11";
                return false;
            }

            const int attribs[] = {
                EGL_PLATFORM_X11_SCREEN_EXT, m_backend->screenNumer(),
                EGL_NONE
            };

            dpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_X11_EXT, m_backend->display(), attribs);
        } else {
            dpy = eglGetDisplay(m_backend->display());
        }
    }

    if (dpy == EGL_NO_DISPLAY) {
        qCWarning(KWIN_X11WINDOWED) << "Failed to get the EGLDisplay";
        return false;
    }
    setEglDisplay(dpy);
    initEglAPI();

    initBufferConfigs();

    if (!createSurfaces()) {
        qCCritical(KWIN_X11WINDOWED) << "Creating egl surface failed";
        return false;
    }

    if (!createContext()) {
        qCCritical(KWIN_X11WINDOWED) << "Create OpenGL context failed";
        return false;
    }

    if (!makeContextCurrent(surface())) {
        qCCritical(KWIN_X11WINDOWED) << "Make Context Current failed";
        return false;
    }

    EGLint error = eglGetError();
    if (error != EGL_SUCCESS) {
        qCWarning(KWIN_X11WINDOWED) << "Error occurred while creating context " << error;
        return false;
    }

    return true;
}

bool EglX11Backend::initBufferConfigs()
{
    initBufferAge();
    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE,         EGL_WINDOW_BIT | (supportsBufferAge() ? 0 : EGL_SWAP_BEHAVIOR_PRESERVED_BIT),
        EGL_RED_SIZE,             1,
        EGL_GREEN_SIZE,           1,
        EGL_BLUE_SIZE,            1,
        EGL_ALPHA_SIZE,           0,
        EGL_RENDERABLE_TYPE,      isOpenGLES() ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_BIT,
        EGL_CONFIG_CAVEAT,        EGL_NONE,
        EGL_NONE,
    };

    EGLint count;
    EGLConfig configs[1024];
    if (eglChooseConfig(eglDisplay(), config_attribs, configs, 1024, &count) == EGL_FALSE) {
        qCCritical(KWIN_X11WINDOWED) << "choose config failed";
        return false;
    }

    ScopedCPointer<xcb_get_window_attributes_reply_t>
            attribs(xcb_get_window_attributes_reply(m_backend->connection(),
                        xcb_get_window_attributes_unchecked(m_backend->connection(),
                                                            m_backend->rootWindow()),
                                                    nullptr));
    if (!attribs) {
        qCCritical(KWIN_X11WINDOWED) << "Failed to get window attributes of root window";
        return false;
    }

    setConfig(configs[0]);
    for (int i = 0; i < count; i++) {
        EGLint val;
        if (eglGetConfigAttrib(eglDisplay(), configs[i], EGL_NATIVE_VISUAL_ID, &val) == EGL_FALSE) {
            qCCritical(KWIN_X11WINDOWED) << "egl get config attrib failed";
        }
        if (uint32_t(val) == attribs->visual) {
            setConfig(configs[i]);
            break;
        }
    }
    return true;
}

bool EglX11Backend::createSurfaces()
{
    for (int i = 0; i < screens()->count(); ++i) {
        EGLSurface s = createSurface(m_backend->windowForScreen(i));
        if (s == EGL_NO_SURFACE) {
            return false;
        }
        m_surfaces << s;
    }
    if (m_surfaces.isEmpty()) {
        return false;
    }
    setSurface(m_surfaces.first());
    return true;
}

EGLSurface EglX11Backend::createSurface(xcb_window_t window)
{
    if (window == XCB_WINDOW_NONE) {
        return EGL_NO_SURFACE;
    }

    EGLSurface surface = EGL_NO_SURFACE;
    if (m_havePlatformBase) {
        // Note: Window is 64 bits on a 64-bit architecture whereas xcb_window_t is
        //       always 32 bits. eglCreatePlatformWindowSurfaceEXT() expects the
        //       native_window parameter to be pointer to a Window, so this variable
        //       cannot be an xcb_window_t.
        surface = eglCreatePlatformWindowSurfaceEXT(eglDisplay(), config(), (void *) &window, nullptr);
    } else {
        surface = eglCreateWindowSurface(eglDisplay(), config(), window, nullptr);
    }

    return surface;
}

bool EglX11Backend::makeContextCurrent(const EGLSurface &surface)
{
    return eglMakeCurrent(eglDisplay(), surface, surface, context()) == EGL_TRUE;
}

void EglX11Backend::present()
{
    // Unused, per-screen presenting.
}

QRegion EglX11Backend::prepareRenderingFrame()
{
    startRenderTimer();
    return QRegion();
}

void EglX11Backend::endRenderingFrame(const QRegion &renderedRegion, const QRegion &damagedRegion)
{
    Q_UNUSED(renderedRegion)
    Q_UNUSED(damagedRegion)
}

void EglX11Backend::screenGeometryChanged(const QSize &size)
{
    Q_UNUSED(size)

    // TODO: base implementation in OpenGLBackend

    // The back buffer contents are now undefined
    m_bufferAge = 0;
}

SceneOpenGLTexturePrivate *EglX11Backend::createBackendTexture(SceneOpenGLTexture *texture)
{
    return new EglTexture(texture, this);
}

bool EglX11Backend::usesOverlayWindow() const
{
    return false;
}

bool EglX11Backend::perScreenRendering() const
{
    return true;
}

QRegion EglX11Backend::prepareRenderingForScreen(int screenId)
{
    makeContextCurrent(m_surfaces.at(screenId));
    setupViewport(screenId);
    return screens()->geometry(screenId);
}

void EglX11Backend::setupViewport(int screenId)
{
    // TODO: ensure the viewport is set correctly each time
    const QSize &overall = screens()->size();
    const QRect &v = screens()->geometry(screenId);
    // TODO: are the values correct?

    qreal scale = screens()->scale(screenId);
    glViewport(-v.x(), v.height() - overall.height() + v.y(), overall.width() * scale, overall.height() * scale);
}

void EglX11Backend::endRenderingFrameForScreen(int screenId, const QRegion &renderedRegion, const QRegion &damagedRegion)
{
    Q_UNUSED(damagedRegion)
    const QRect &outputGeometry = screens()->geometry(screenId);
    presentSurface(m_surfaces.at(screenId), renderedRegion, outputGeometry);
}

void EglX11Backend::presentSurface(EGLSurface surface, const QRegion &damage, const QRect &screenGeometry)
{
    if (damage.isEmpty()) {
        return;
    }
    const bool fullRepaint = supportsBufferAge() || (damage == screenGeometry);

    if (fullRepaint || !m_surfaceHasSubPost) {
        // the entire screen changed, or we cannot do partial updates (which implies we enabled surface preservation)
        eglSwapBuffers(eglDisplay(), surface);
        if (supportsBufferAge()) {
            eglQuerySurface(eglDisplay(), surface, EGL_BUFFER_AGE_EXT, &m_bufferAge);
        }
    } else {
        // a part of the screen changed, and we can use eglPostSubBufferNV to copy the updated area
        for (const QRect &r : damage) {
            eglPostSubBufferNV(eglDisplay(), surface, r.left(), screenGeometry.height() - r.bottom() - 1, r.width(), r.height());
        }
    }
}

/************************************************
 * EglTexture
 ************************************************/

EglTexture::EglTexture(KWin::SceneOpenGLTexture *texture, KWin::EglX11Backend *backend)
    : AbstractEglTexture(texture, backend)
    , m_backend(backend)
{
}

void KWin::EglTexture::onDamage()
{
    if (options->isGlStrictBinding()) {
        // This is just implemented to be consistent with
        // the example in mesa/demos/src/egl/opengles1/texture_from_pixmap.c
        eglWaitNative(EGL_CORE_NATIVE_ENGINE);
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES) image());
    }
    GLTexturePrivate::onDamage();
}

} // namespace
