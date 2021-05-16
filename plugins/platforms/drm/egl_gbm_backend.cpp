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
#include "egl_gbm_backend.h"
// kwin
#include "composite.h"
#include "drm_backend.h"
#include "drm_output.h"
#include "gbm_surface.h"
#include "logging.h"
#include "options.h"
#include "screens.h"
// kwin libs
#include <kwinglplatform.h>
// Qt
#include <QOpenGLContext>
// system
#include <gbm.h>

namespace KWin
{

EglGbmBackend::EglGbmBackend(DrmBackend *drmBackend)
    : AbstractEglBackend()
    , m_backend(drmBackend)
{
    // Egl is always direct rendering.
    setIsDirectRendering(true);

    connect(m_backend, &DrmBackend::output_added, this, [this](auto output) {
        createOutput(static_cast<DrmOutput*>(output));
    });
    connect(m_backend, &DrmBackend::output_removed, this, [this](auto output) {
        removeOutput(static_cast<DrmOutput*>(output));
    });
}

EglGbmBackend::~EglGbmBackend()
{
    cleanup();
}

void EglGbmBackend::cleanupSurfaces()
{
    for (auto it = m_outputs.begin(); it != m_outputs.end(); ++it) {
        cleanupOutput(*it);
    }
    m_outputs.clear();
}

void EglGbmBackend::cleanupFramebuffer(Output &output)
{
    if (!output.render.framebuffer) {
        return;
    }
    glDeleteTextures(1, &output.render.texture);
    output.render.texture = 0;
    glDeleteFramebuffers(1, &output.render.framebuffer);
    output.render.framebuffer = 0;
}

void EglGbmBackend::cleanupOutput(Output &output)
{
    cleanupFramebuffer(output);
    output.output->releaseGbm();

    if (output.eglSurface != EGL_NO_SURFACE) {
        eglDestroySurface(eglDisplay(), output.eglSurface);
    }
}

bool EglGbmBackend::initializeEgl()
{
    initClientExtensions();
    EGLDisplay display = m_backend->sceneEglDisplay();

    // Use eglGetPlatformDisplayEXT() to get the display pointer
    // if the implementation supports it.
    if (display == EGL_NO_DISPLAY) {
        const bool hasMesaGBM = hasClientExtension(QByteArrayLiteral("EGL_MESA_platform_gbm"));
        const bool hasKHRGBM = hasClientExtension(QByteArrayLiteral("EGL_KHR_platform_gbm"));
        const GLenum platform = hasMesaGBM ? EGL_PLATFORM_GBM_MESA : EGL_PLATFORM_GBM_KHR;

        if (!hasClientExtension(QByteArrayLiteral("EGL_EXT_platform_base")) ||
                (!hasMesaGBM && !hasKHRGBM)) {
            setFailed("Missing one or more extensions between EGL_EXT_platform_base, "
                      "EGL_MESA_platform_gbm, EGL_KHR_platform_gbm");
            return false;
        }

        auto device = gbm_create_device(m_backend->fd());
        if (!device) {
            setFailed("Could not create gbm device");
            return false;
        }
        m_backend->setGbmDevice(device);

        display = eglGetPlatformDisplayEXT(platform, device, nullptr);
    }

    if (display == EGL_NO_DISPLAY) {
        return false;
    }
    setEglDisplay(display);
    return initEglAPI();
}

void EglGbmBackend::init()
{
    if (!initializeEgl()) {
        setFailed("Could not initialize egl");
        return;
    }
    if (!initRenderingContext()) {
        setFailed("Could not initialize rendering context");
        return;
    }

    initKWinGL();
    initBufferAge();
    initWayland();
}

bool EglGbmBackend::initRenderingContext()
{
    initBufferConfigs();
    if (!createContext()) {
        return false;
    }

    const auto outputs = m_backend->drmOutputs();

    for (DrmOutput *drmOutput: outputs) {
        createOutput(drmOutput);
    }

    if (m_outputs.isEmpty()) {
        qCCritical(KWIN_DRM) << "Create Window Surfaces failed";
        return false;
    }

    // Set our first surface as the one for the abstract backend, just to make it happy.
    setSurface(m_outputs.first().eglSurface);

    return makeContextCurrent(m_outputs.first());
}

std::shared_ptr<GbmSurface> EglGbmBackend::createGbmSurface(const QSize &size) const
{
    auto gbmSurface = std::make_shared<GbmSurface>(m_backend->gbmDevice(),
                                                   size.width(), size.height(),
                                                   GBM_FORMAT_XRGB8888,
                                                   GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!gbmSurface) {
        qCCritical(KWIN_DRM) << "Creating GBM surface failed";
        return nullptr;
    }
    return gbmSurface;
}

EGLSurface EglGbmBackend::createEglSurface(std::shared_ptr<GbmSurface> gbmSurface) const
{
    auto eglSurface = eglCreatePlatformWindowSurfaceEXT(eglDisplay(), config(),
                                                        (void *)(gbmSurface->surface()), nullptr);
    if (eglSurface == EGL_NO_SURFACE) {
        qCCritical(KWIN_DRM) << "Creating EGL surface failed";
        return EGL_NO_SURFACE;
    }
    return eglSurface;
}

bool EglGbmBackend::resetOutput(Output &output, DrmOutput *drmOutput)
{
    output.output = drmOutput;
    auto size = drmOutput->viewGeometry().size();
    if (!drmOutput->hardwareTransforms()) {
        size = drmOutput->orientateSize(size);
    }

    auto gbmSurface = createGbmSurface(size);
    if (!gbmSurface) {
        return false;
    }
    auto eglSurface = createEglSurface(gbmSurface);
    if (eglSurface == EGL_NO_SURFACE) {
        return false;
    }

    // destroy previous surface
    if (output.eglSurface != EGL_NO_SURFACE) {
        if (surface() == output.eglSurface) {
            setSurface(eglSurface);
        }
        eglDestroySurface(eglDisplay(), output.eglSurface);
    }
    output.eglSurface = eglSurface;
    output.gbmSurface = gbmSurface;

    resetFramebuffer(output);
    return true;
}

EglGbmBackend::Output& EglGbmBackend::get_output(AbstractOutput* output)
{
    for (auto& out: m_outputs) {
        if (out.output == output) {
            return out;
        }
    }
    assert(false);
    return m_outputs[0];
}

void EglGbmBackend::createOutput(DrmOutput *drmOutput)
{
    Output newOutput;
    if (resetOutput(newOutput, drmOutput)) {
        connect(drmOutput, &DrmOutput::modeChanged, this,
            [drmOutput, this] {
                auto it = std::find_if(m_outputs.begin(), m_outputs.end(),
                    [drmOutput] (const auto &output) {
                        return output.output == drmOutput;
                    }
                );
                if (it == m_outputs.end()) {
                    return;
                }
                resetOutput(*it, drmOutput);
            }
        );
        m_outputs << newOutput;
    }
}

void EglGbmBackend::removeOutput(DrmOutput *drmOutput)
{
    auto it = std::find_if(m_outputs.begin(), m_outputs.end(),
        [drmOutput] (const Output &output) {
            return output.output == drmOutput;
        }
    );
    if (it == m_outputs.end()) {
        return;
    }
    cleanupOutput(*it);
    m_outputs.erase(it);
}

const float vertices[] = {
   -1.0f,  1.0f,
   -1.0f, -1.0f,
    1.0f, -1.0f,

   -1.0f,  1.0f,
    1.0f, -1.0f,
    1.0f,  1.0f,
};

const float texCoords[] = {
    0.0f,  1.0f,
    0.0f,  0.0f,
    1.0f,  0.0f,

    0.0f,  1.0f,
    1.0f,  0.0f,
    1.0f,  1.0f
};

bool EglGbmBackend::resetFramebuffer(Output &output)
{
    cleanupFramebuffer(output);

    if (output.output->hardwareTransforms()) {
        // No need for an extra render target.
        return true;
    }

    makeContextCurrent(output);

    glGenFramebuffers(1, &output.render.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, output.render.framebuffer);
    GLRenderTarget::setKWinFramebuffer(output.render.framebuffer);

    glGenTextures(1, &output.render.texture);
    glBindTexture(GL_TEXTURE_2D, output.render.texture);

    const QSize texSize = output.output->viewGeometry().size();

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texSize.width(), texSize.height(),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glBindTexture(GL_TEXTURE_2D, 0);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           output.render.texture, 0);

    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        qCWarning(KWIN_DRM) << "Error: framebuffer not complete";
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    GLRenderTarget::setKWinFramebuffer(0);
    return true;
}

void EglGbmBackend::initRenderTarget(Output &output)
{
    if (output.render.vbo) {
        // Already initialized.
        return;
    }
    std::shared_ptr<GLVertexBuffer> vbo(new GLVertexBuffer(KWin::GLVertexBuffer::Static));
    vbo->setData(6, 2, vertices, texCoords);
    output.render.vbo = vbo;
}

void EglGbmBackend::renderFramebufferToSurface(Output &output)
{
    if (!output.render.framebuffer) {
        // No additional render target.
        return;
    }
    initRenderTarget(output);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    GLRenderTarget::setKWinFramebuffer(0);

    auto size = output.output->viewGeometry().size();
    if (!output.output->hardwareTransforms()) {
        size = output.output->orientateSize(size);
    }
    glViewport(0, 0, size.width(), size.height());

    auto shader = ShaderManager::instance()->pushShader(ShaderTrait::MapTexture);

    QMatrix4x4 rotationMatrix;
    rotationMatrix.rotate(output.output->rotation(), 0, 0, 1);
    shader->setUniform(GLShader::ModelViewProjectionMatrix, rotationMatrix);

    glBindTexture(GL_TEXTURE_2D, output.render.texture);
    output.render.vbo->render(GL_TRIANGLES);
    ShaderManager::instance()->popShader();
}

void EglGbmBackend::prepareRenderFramebuffer(const Output &output) const
{
    // When render.framebuffer is 0 we may just reset to the screen framebuffer.
    glBindFramebuffer(GL_FRAMEBUFFER, output.render.framebuffer);
    GLRenderTarget::setKWinFramebuffer(output.render.framebuffer);
}

bool EglGbmBackend::makeContextCurrent(const Output &output) const
{
    const EGLSurface surface = output.eglSurface;
    if (surface == EGL_NO_SURFACE) {
        return false;
    }
    if (eglMakeCurrent(eglDisplay(), surface, surface, context()) == EGL_FALSE) {
        qCCritical(KWIN_DRM) << "Make Context Current failed" << eglGetError();
        return false;
    }
    return true;
}

bool EglGbmBackend::initBufferConfigs()
{
    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE,         EGL_WINDOW_BIT,
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
    if (!eglChooseConfig(eglDisplay(), config_attribs, configs,
                         sizeof(configs) / sizeof(EGLConfig),
                         &count)) {
        qCCritical(KWIN_DRM) << "choose config failed";
        return false;
    }

    qCDebug(KWIN_DRM) << "EGL buffer configs count:" << count;

    // Loop through all configs, choosing the first one that has suitable format.
    for (EGLint i = 0; i < count; i++) {
        EGLint gbmFormat;
        // Query some configuration parameters, to show in debug log.
        eglGetConfigAttrib(eglDisplay(), configs[i], EGL_NATIVE_VISUAL_ID, &gbmFormat);

        if (KWIN_DRM().isDebugEnabled()) {
            // GBM formats are declared as FOURCC code (integer from ASCII chars, so use this fact).
            char gbmFormatStr[sizeof(EGLint) + 1] = {0};
            memcpy(gbmFormatStr, &gbmFormat, sizeof(EGLint));

            // Query number of bits for color channel.
            EGLint blueSize, redSize, greenSize, alphaSize;
            eglGetConfigAttrib(eglDisplay(), configs[i], EGL_RED_SIZE, &redSize);
            eglGetConfigAttrib(eglDisplay(), configs[i], EGL_GREEN_SIZE, &greenSize);
            eglGetConfigAttrib(eglDisplay(), configs[i], EGL_BLUE_SIZE, &blueSize);
            eglGetConfigAttrib(eglDisplay(), configs[i], EGL_ALPHA_SIZE, &alphaSize);
            qCDebug(KWIN_DRM) << "  EGL config #" << i << " has GBM FOURCC format:" << gbmFormatStr
                              << "; color sizes (RGBA order):"
                              << redSize << greenSize << blueSize << alphaSize;
        }

        if ((gbmFormat == GBM_FORMAT_XRGB8888) || (gbmFormat == GBM_FORMAT_ARGB8888)) {
            setConfig(configs[i]);
            return true;
        }
    }

    qCCritical(KWIN_DRM) << "Choosing EGL config did not return a suitable config. There were"
                         << count << "configs.";
    return false;
}

void EglGbmBackend::present()
{
    Q_UNREACHABLE();
    // Not in use. This backend does per-screen rendering.
}

void EglGbmBackend::presentOnOutput(Output &output)
{
    eglSwapBuffers(eglDisplay(), output.eglSurface);
    output.buffer = m_backend->createBuffer(output.gbmSurface);
    m_backend->present(output.buffer, output.output);
}

void EglGbmBackend::screenGeometryChanged(const QSize &size)
{
    Q_UNUSED(size)
    // TODO, create new buffer?
}

SceneOpenGLTexturePrivate *EglGbmBackend::createBackendTexture(SceneOpenGLTexture *texture)
{
    return new EglGbmTexture(texture, this);
}

QRegion EglGbmBackend::prepareRenderingFrame()
{
    startRenderTimer();
    return QRegion();
}

void EglGbmBackend::setViewport(const Output &output) const
{
    const QSize &overall = screens()->size();
    const QRect &geo = output.output->geometry();
    const QRect &view = output.output->viewGeometry();

    const qreal widthRatio = view.width() / (double) geo.width();
    const qreal heightRatio = view.height() / (double) geo.height();

    glViewport(-geo.x() * widthRatio,
               (geo.height() - overall.height() + geo.y()) * heightRatio,
               overall.width() * widthRatio,
               overall.height() * heightRatio);
}

QRegion EglGbmBackend::prepareRenderingForScreen(AbstractOutput* output)
{
    auto const& out = get_output(output);

    makeContextCurrent(out);
    prepareRenderFramebuffer(out);
    setViewport(out);

    if (supportsBufferAge()) {
        QRegion region;

        // Note: An age of zero means the buffer contents are undefined
        if (out.bufferAge > 0 && out.bufferAge <= out.damageHistory.count()) {
            for (int i = 0; i < out.bufferAge - 1; i++)
                region |= out.damageHistory[i];
        } else {
            region = output->geometry();
        }

        return region;
    }
    return output->geometry();
}

void EglGbmBackend::endRenderingFrame(const QRegion &renderedRegion, const QRegion &damagedRegion)
{
    Q_UNUSED(renderedRegion)
    Q_UNUSED(damagedRegion)
}

void EglGbmBackend::endRenderingFrameForScreen(AbstractOutput* output,
                                               const QRegion &renderedRegion,
                                               const QRegion &damagedRegion)
{
    auto& out = get_output(output);
    renderFramebufferToSurface(out);

    auto const output_damage = damagedRegion.intersected(output->geometry());

    if (output_damage.isEmpty()) {

        // If the damaged region of a window is fully occluded, the only
        // rendering done, if any, will have been to repair a reused back
        // buffer, making it identical to the front buffer.
        //
        // In this case we won't post the back buffer. Instead we'll just
        // set the buffer age to 1, so the repaired regions won't be
        // rendered again in the next frame.
        if (!renderedRegion.intersected(output->geometry()).isEmpty())
            glFlush();

        out.bufferAge = 1;
        return;
    }
    presentOnOutput(out);

    if (supportsBufferAge()) {
        eglQuerySurface(eglDisplay(), out.eglSurface, EGL_BUFFER_AGE_EXT, &out.bufferAge);
        if (out.damageHistory.count() > 10) {
            out.damageHistory.removeLast();
        }
        out.damageHistory.prepend(output_damage);
    }
}

bool EglGbmBackend::usesOverlayWindow() const
{
    return false;
}

/************************************************
 * EglTexture
 ************************************************/

EglGbmTexture::EglGbmTexture(KWin::SceneOpenGLTexture *texture, EglGbmBackend *backend)
    : EglTexture(texture, backend)
{
}

EglGbmTexture::~EglGbmTexture() = default;

}
