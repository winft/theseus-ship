/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2010 by Fredrik Höglund <fredrik@kde.org>
Copyright (C) 2010 Martin Gräßlin <mgraesslin@kde.org>

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
#include "lanczosfilter.h"

#include "effects.h"
#include "options.h"
#include "screens.h"
#include "workspace.h"

#include <logging.h>

#include <kwinglplatform.h>
#include <kwinglutils.h>

#include <kwineffects.h>

#include <QFile>
#include <QtMath>

#include <cmath>

namespace KWin::render::gl
{

lanczos_filter::lanczos_filter(scene* parent)
    : QObject(parent)
    , m_offscreenTex(nullptr)
    , m_offscreenTarget(nullptr)
    , m_inited(false)
    , m_shader(nullptr)
    , m_uOffsets(0)
    , m_uKernel(0)
    , m_scene(parent)
{
}

lanczos_filter::~lanczos_filter()
{
    delete m_offscreenTarget;
    delete m_offscreenTex;
}

void lanczos_filter::init()
{
    if (m_inited)
        return;
    m_inited = true;
    const bool force = (qstrcmp(qgetenv("KWIN_FORCE_LANCZOS"), "1") == 0);
    if (force) {
        qCWarning(KWIN_OPENGL) << "Lanczos Filter forced on by environment variable";
    }

    if (!GLRenderTarget::supported())
        return;

    GLPlatform* gl = GLPlatform::instance();
    if (!force) {
        // The lanczos filter is reported to be broken with the Intel driver prior SandyBridge
        if (gl->driver() == Driver_Intel && gl->chipClass() < SandyBridge)
            return;
        // also radeon before R600 has trouble
        if (gl->isRadeon() && gl->chipClass() < R600)
            return;
        // and also for software emulation (e.g. llvmpipe)
        if (gl->isSoftwareEmulation()) {
            return;
        }
    }
    QFile ff(gl->glslVersion() >= kVersionNumber(1, 40)
                 ? QStringLiteral(":/scenes/opengl/shaders/1.40/lanczos-fragment.glsl")
                 : QStringLiteral(":/scenes/opengl/shaders/1.10/lanczos-fragment.glsl"));
    if (!ff.open(QIODevice::ReadOnly)) {
        qCDebug(KWIN_OPENGL) << "Failed to open lanczos shader";
        return;
    }
    m_shader.reset(ShaderManager::instance()->generateCustomShader(
        ShaderTrait::MapTexture, QByteArray(), ff.readAll()));
    if (m_shader->isValid()) {
        ShaderBinder binder(m_shader.data());
        m_uKernel = m_shader->uniformLocation("kernel");
        m_uOffsets = m_shader->uniformLocation("offsets");
    } else {
        qCDebug(KWIN_OPENGL) << "Shader is not valid";
        m_shader.reset();
    }
}

void lanczos_filter::updateOffscreenSurfaces()
{
    const QSize& s = screens()->size();
    int w = s.width();
    int h = s.height();

    if (!m_offscreenTex || m_offscreenTex->width() != w || m_offscreenTex->height() != h) {
        if (m_offscreenTex) {
            delete m_offscreenTex;
            delete m_offscreenTarget;
        }
        m_offscreenTex = new GLTexture(GL_RGBA8, w, h);
        m_offscreenTex->setFilter(GL_LINEAR);
        m_offscreenTex->setWrapMode(GL_CLAMP_TO_EDGE);
        m_offscreenTarget = new GLRenderTarget(*m_offscreenTex);
    }
}

static float sinc(float x)
{
    return std::sin(x * M_PI) / (x * M_PI);
}

static float lanczos(float x, float a)
{
    if (qFuzzyCompare(x + 1.0, 1.0))
        return 1.0;

    if (qAbs(x) >= a)
        return 0.0;

    return sinc(x) * sinc(x / a);
}

void lanczos_filter::createKernel(float delta, int* size)
{
    const float a = 2.0;

    // The two outermost samples always fall at points where the lanczos
    // function returns 0, so we'll skip them.
    const int sampleCount = qBound(3, qCeil(delta * a) * 2 + 1 - 2, 29);
    const int center = sampleCount / 2;
    const int kernelSize = center + 1;
    const float factor = 1.0 / delta;

    QVector<float> values(kernelSize);
    float sum = 0;

    for (int i = 0; i < kernelSize; i++) {
        const float val = lanczos(i * factor, a);
        sum += i > 0 ? val * 2 : val;
        values[i] = val;
    }

    m_kernel.fill(QVector4D());

    // Normalize the kernel
    for (int i = 0; i < kernelSize; i++) {
        const float val = values[i] / sum;
        m_kernel[i] = QVector4D(val, val, val, val);
    }

    *size = kernelSize;
}

void lanczos_filter::createOffsets(int count, float width, Qt::Orientation direction)
{
    m_offsets.fill(QVector2D());
    for (int i = 0; i < count; i++) {
        m_offsets[i]
            = (direction == Qt::Horizontal) ? QVector2D(i / width, 0) : QVector2D(0, i / width);
    }
}

void lanczos_filter::performPaint(EffectWindowImpl* w,
                                  paint_type mask,
                                  QRegion region,
                                  WindowPaintData& data)
{
    if (data.xScale() < 0.9 || data.yScale() < 0.9) {
        if (!m_inited)
            init();
        const QRect screenRect
            = Workspace::self()->clientArea(ScreenArea, w->screen(), w->desktop());
        // window geometry may not be bigger than screen geometry to fit into the FBO
        QRect winGeo(w->expandedGeometry());
        if (m_shader && winGeo.width() <= screenRect.width()
            && winGeo.height() <= screenRect.height()) {
            winGeo.translate(-w->geometry().topLeft());
            double left = winGeo.left();
            double top = winGeo.top();
            double width = winGeo.right() - left;
            double height = winGeo.bottom() - top;

            int tx = data.xTranslation() + w->x() + left * data.xScale();
            int ty = data.yTranslation() + w->y() + top * data.yScale();
            int tw = width * data.xScale();
            int th = height * data.yScale();
            const QRect textureRect(tx, ty, tw, th);
            const bool hardwareClipping = !(QRegion(textureRect) - region).isEmpty();

            int sw = width;
            int sh = height;

            GLTexture* cachedTexture
                = static_cast<GLTexture*>(w->data(LanczosCacheRole).value<void*>());
            if (cachedTexture) {
                if (cachedTexture->width() == tw && cachedTexture->height() == th) {
                    cachedTexture->bind();
                    if (hardwareClipping) {
                        glEnable(GL_SCISSOR_TEST);
                    }

                    glEnable(GL_BLEND);
                    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

                    const qreal rgb = data.brightness() * data.opacity();
                    const qreal a = data.opacity();

                    ShaderBinder binder(QFlags({ShaderTrait::MapTexture,
                                                ShaderTrait::Modulate,
                                                ShaderTrait::AdjustSaturation}));
                    GLShader* shader = binder.shader();
                    QMatrix4x4 mvp = data.screenProjectionMatrix();
                    mvp.translate(textureRect.x(), textureRect.y());
                    shader->setUniform(GLShader::ModelViewProjectionMatrix, mvp);
                    shader->setUniform(GLShader::ModulationConstant, QVector4D(rgb, rgb, rgb, a));
                    shader->setUniform(GLShader::Saturation, data.saturation());

                    cachedTexture->render(region, textureRect, hardwareClipping);

                    glDisable(GL_BLEND);
                    if (hardwareClipping) {
                        glDisable(GL_SCISSOR_TEST);
                    }
                    cachedTexture->unbind();
                    m_timer.start(5000, this);
                    return;
                } else {
                    // offscreen texture not matching - delete
                    delete cachedTexture;
                    cachedTexture = nullptr;
                    w->setData(LanczosCacheRole, QVariant());
                }
            }

            WindowPaintData thumbData = data;
            thumbData.setXScale(1.0);
            thumbData.setYScale(1.0);
            thumbData.setXTranslation(-w->x() - left);
            thumbData.setYTranslation(-w->y() - top);
            thumbData.setBrightness(1.0);
            thumbData.setOpacity(1.0);
            thumbData.setSaturation(1.0);

            // Bind the offscreen FBO and draw the window on it unscaled
            updateOffscreenSurfaces();
            GLRenderTarget::pushRenderTarget(m_offscreenTarget);

            QMatrix4x4 modelViewProjectionMatrix;
            modelViewProjectionMatrix.ortho(
                0, m_offscreenTex->width(), m_offscreenTex->height(), 0, 0, 65535);
            thumbData.setProjectionMatrix(modelViewProjectionMatrix);

            glClearColor(0.0, 0.0, 0.0, 0.0);
            glClear(GL_COLOR_BUFFER_BIT);
            w->sceneWindow()->performPaint(mask, infiniteRegion(), thumbData);

            // Create a scratch texture and copy the rendered window into it
            GLTexture tex(GL_RGBA8, sw, sh);
            tex.setFilter(GL_LINEAR);
            tex.setWrapMode(GL_CLAMP_TO_EDGE);
            tex.bind();

            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, m_offscreenTex->height() - sh, sw, sh);

            // Set up the shader for horizontal scaling
            float dx = sw / float(tw);
            int kernelSize;
            createKernel(dx, &kernelSize);
            createOffsets(kernelSize, sw, Qt::Horizontal);

            ShaderManager::instance()->pushShader(m_shader.data());
            m_shader->setUniform(GLShader::ModelViewProjectionMatrix, modelViewProjectionMatrix);
            setUniforms();

            // Draw the window back into the FBO, this time scaled horizontally
            glClear(GL_COLOR_BUFFER_BIT);
            QVector<float> verts;
            QVector<float> texCoords;
            verts.reserve(12);
            texCoords.reserve(12);

            texCoords << 1.0 << 0.0;
            verts << tw << 0.0; // Top right
            texCoords << 0.0 << 0.0;
            verts << 0.0 << 0.0; // Top left
            texCoords << 0.0 << 1.0;
            verts << 0.0 << sh; // Bottom left
            texCoords << 0.0 << 1.0;
            verts << 0.0 << sh; // Bottom left
            texCoords << 1.0 << 1.0;
            verts << tw << sh; // Bottom right
            texCoords << 1.0 << 0.0;
            verts << tw << 0.0; // Top right
            GLVertexBuffer* vbo = GLVertexBuffer::streamingBuffer();
            vbo->reset();
            vbo->setData(6, 2, verts.constData(), texCoords.constData());
            vbo->render(GL_TRIANGLES);

            // At this point we don't need the scratch texture anymore
            tex.unbind();
            tex.discard();

            // create scratch texture for second rendering pass
            GLTexture tex2(GL_RGBA8, tw, sh);
            tex2.setFilter(GL_LINEAR);
            tex2.setWrapMode(GL_CLAMP_TO_EDGE);
            tex2.bind();

            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, m_offscreenTex->height() - sh, tw, sh);

            // Set up the shader for vertical scaling
            float dy = sh / float(th);
            createKernel(dy, &kernelSize);
            createOffsets(kernelSize, m_offscreenTex->height(), Qt::Vertical);
            setUniforms();

            // Now draw the horizontally scaled window in the FBO at the right
            // coordinates on the screen, while scaling it vertically and blending it.
            glClear(GL_COLOR_BUFFER_BIT);

            verts.clear();

            verts << tw << 0.0;  // Top right
            verts << 0.0 << 0.0; // Top left
            verts << 0.0 << th;  // Bottom left
            verts << 0.0 << th;  // Bottom left
            verts << tw << th;   // Bottom right
            verts << tw << 0.0;  // Top right
            vbo->setData(6, 2, verts.constData(), texCoords.constData());
            vbo->render(GL_TRIANGLES);

            tex2.unbind();
            tex2.discard();
            ShaderManager::instance()->popShader();

            // create cache texture
            GLTexture* cache = new GLTexture(GL_RGBA8, tw, th);

            cache->setFilter(GL_LINEAR);
            cache->setWrapMode(GL_CLAMP_TO_EDGE);
            cache->bind();
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, m_offscreenTex->height() - th, tw, th);
            GLRenderTarget::popRenderTarget();

            if (hardwareClipping) {
                glEnable(GL_SCISSOR_TEST);
            }

            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

            const qreal rgb = data.brightness() * data.opacity();
            const qreal a = data.opacity();

            ShaderBinder binder(QFlags(
                {ShaderTrait::MapTexture, ShaderTrait::Modulate, ShaderTrait::AdjustSaturation}));
            GLShader* shader = binder.shader();
            QMatrix4x4 mvp = data.screenProjectionMatrix();
            mvp.translate(textureRect.x(), textureRect.y());
            shader->setUniform(GLShader::ModelViewProjectionMatrix, mvp);
            shader->setUniform(GLShader::ModulationConstant, QVector4D(rgb, rgb, rgb, a));
            shader->setUniform(GLShader::Saturation, data.saturation());

            cache->render(region, textureRect, hardwareClipping);

            glDisable(GL_BLEND);

            if (hardwareClipping) {
                glDisable(GL_SCISSOR_TEST);
            }

            cache->unbind();
            w->setData(LanczosCacheRole, QVariant::fromValue(static_cast<void*>(cache)));

            // Delete the offscreen surface after 5 seconds
            m_timer.start(5000, this);
            return;
        }
    } // if ( effects->compositingType() == KWin::OpenGLCompositing )
    w->sceneWindow()->performPaint(mask, region, data);
} // End of function

void lanczos_filter::timerEvent(QTimerEvent* event)
{
    if (event->timerId() == m_timer.timerId()) {
        m_timer.stop();

        m_scene->makeOpenGLContextCurrent();

        delete m_offscreenTarget;
        delete m_offscreenTex;
        m_offscreenTarget = nullptr;
        m_offscreenTex = nullptr;

        workspace()->forEachToplevel(
            [this](Toplevel* toplevel) { discardCacheTexture(toplevel->effectWindow()); });

        m_scene->doneOpenGLContextCurrent();
    }
}

void lanczos_filter::discardCacheTexture(EffectWindow* w)
{
    QVariant cachedTextureVariant = w->data(LanczosCacheRole);
    if (cachedTextureVariant.isValid()) {
        delete static_cast<GLTexture*>(cachedTextureVariant.value<void*>());
        w->setData(LanczosCacheRole, QVariant());
    }
}

void lanczos_filter::setUniforms()
{
    glUniform2fv(m_uOffsets, m_offsets.size(), (const GLfloat*)m_offsets.data());
    glUniform4fv(m_uKernel, m_kernel.size(), (const GLfloat*)m_kernel.data());
}

} // namespace
