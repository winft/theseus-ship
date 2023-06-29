/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2010 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/types.h"
#include "win/window_area.h"

#include <kwineffects/effect_window.h>
#include <kwineffects/paint_data.h>
#include <kwingl/platform.h>
#include <kwingl/utils.h>

#include <QObject>
#include <QVector2D>
#include <QVector4D>
#include <QVector>
#include <QtMath>
#include <array>
#include <cmath>

namespace KWin::render::gl
{

template<typename Scene>
class lanczos_filter : public QObject
{
public:
    explicit lanczos_filter(Scene* parent)
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

    ~lanczos_filter() override
    {
        delete m_offscreenTarget;
        delete m_offscreenTex;
    }

    template<typename EffWinImpl>
    void performPaint(EffWinImpl& eff_win, paint_type mask, effect::window_paint_data& data)
    {
        if (data.paint.geo.scale.x() > 0.9 && data.paint.geo.scale.y() > 0.9) {
            eff_win.window.performPaint(mask, data);
            return;
        }

        init();

        auto const screenRect
            = std::visit(overload{[this](auto&& win) {
                             auto const output = win->topo.central_output;
                             return win::space_window_area(*m_scene->platform.base.space,
                                                           win::area_option::screen,
                                                           output,
                                                           win::get_desktop(*win));
                         }},
                         *eff_win.window.ref_win);

        QRect winGeo(eff_win.expandedGeometry());

        if (!m_shader || winGeo.width() > screenRect.width()
            || winGeo.height() > screenRect.height()) {
            // window geometry may not be bigger than screen geometry to fit into the FBO
            eff_win.window.performPaint(mask, data);
            return;
        }

        winGeo.translate(-eff_win.frameGeometry().topLeft());

        double left = winGeo.left();
        double top = winGeo.top();
        double width = winGeo.right() - left;
        double height = winGeo.bottom() - top;

        auto tx = data.paint.geo.translation.x() + eff_win.x() + left * data.paint.geo.scale.x();
        auto ty = data.paint.geo.translation.y() + eff_win.y() + top * data.paint.geo.scale.y();
        auto tw = width * data.paint.geo.scale.x();
        auto th = height * data.paint.geo.scale.y();
        QRect const textureRect(tx, ty, tw, th);
        auto const hardwareClipping = !(QRegion(textureRect) - data.paint.region).isEmpty();

        auto sw = width;
        auto sh = height;

        QRegion scissor = infiniteRegion();
        if (hardwareClipping) {
            scissor = m_scene->mapToRenderTarget(data.paint.region);
        }

        auto cachedTexture
            = static_cast<GLTexture*>(eff_win.data(LanczosCacheRole).template value<void*>());

        if (cachedTexture) {
            if (cachedTexture->width() == tw && cachedTexture->height() == th) {
                cachedTexture->bind();
                if (hardwareClipping) {
                    glEnable(GL_SCISSOR_TEST);
                }

                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

                ShaderBinder binder(QFlags({ShaderTrait::MapTexture,
                                            ShaderTrait::Modulate,
                                            ShaderTrait::AdjustSaturation}));
                auto shader = binder.shader();
                auto mvp = data.paint.screen_projection_matrix;
                mvp.translate(textureRect.x(), textureRect.y());
                shader->setUniform(GLShader::ModelViewProjectionMatrix, mvp);

                auto const rgb = data.paint.brightness * data.paint.opacity;
                shader->setUniform(GLShader::ModulationConstant,
                                   QVector4D(rgb, rgb, rgb, data.paint.opacity));
                shader->setUniform(GLShader::Saturation, data.paint.saturation);

                cachedTexture->render(scissor, textureRect.size(), hardwareClipping);

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
                eff_win.setData(LanczosCacheRole, QVariant());
            }
        }

        effect::window_paint_data thumbData = data;
        thumbData.paint.region = infiniteRegion();
        thumbData.paint.geo.scale.setX(1.0);
        thumbData.paint.geo.scale.setY(1.0);
        thumbData.paint.geo.translation.setX(-eff_win.x() - left);
        thumbData.paint.geo.translation.setY(-eff_win.y() - top);
        thumbData.paint.brightness = 1.0;
        thumbData.paint.opacity = 1.0;
        thumbData.paint.saturation = 1.0;

        // Bind the offscreen FBO and draw the window on it unscaled
        updateOffscreenSurfaces();
        GLFramebuffer::pushRenderTarget(m_offscreenTarget);

        QMatrix4x4 modelViewProjectionMatrix;
        modelViewProjectionMatrix.ortho(
            0, m_offscreenTex->width(), m_offscreenTex->height(), 0, 0, 65535);
        thumbData.paint.projection_matrix = modelViewProjectionMatrix;

        glClearColor(0.0, 0.0, 0.0, 0.0);
        glClear(GL_COLOR_BUFFER_BIT);
        eff_win.window.performPaint(mask, thumbData);

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

        ShaderManager::instance()->pushShader(m_shader.get());
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
        GLFramebuffer::popRenderTarget();

        if (hardwareClipping) {
            glEnable(GL_SCISSOR_TEST);
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        ShaderBinder binder(QFlags(
            {ShaderTrait::MapTexture, ShaderTrait::Modulate, ShaderTrait::AdjustSaturation}));
        auto shader = binder.shader();
        auto mvp = data.paint.screen_projection_matrix;
        mvp.translate(textureRect.x(), textureRect.y());
        shader->setUniform(GLShader::ModelViewProjectionMatrix, mvp);

        auto const rgb = data.paint.brightness * data.paint.opacity;
        shader->setUniform(GLShader::ModulationConstant,
                           QVector4D(rgb, rgb, rgb, data.paint.opacity));
        shader->setUniform(GLShader::Saturation, data.paint.saturation);

        cache->render(scissor, textureRect.size(), hardwareClipping);

        glDisable(GL_BLEND);

        if (hardwareClipping) {
            glDisable(GL_SCISSOR_TEST);
        }

        cache->unbind();
        eff_win.setData(LanczosCacheRole, QVariant::fromValue(static_cast<void*>(cache)));

        // Delete the offscreen surface after 5 seconds
        m_timer.start(5000, this);
    }

protected:
    void timerEvent(QTimerEvent* event) override
    {
        if (event->timerId() == m_timer.timerId()) {
            m_timer.stop();

            m_scene->makeOpenGLContextCurrent();

            delete m_offscreenTarget;
            delete m_offscreenTex;
            m_offscreenTarget = nullptr;
            m_offscreenTex = nullptr;

            for (auto win : m_scene->platform.base.space->windows) {
                std::visit(
                    overload{[&](auto&& win) { discardCacheTexture(win->render->effect.get()); }},
                    win);
            }

            m_scene->doneOpenGLContextCurrent();
        }
    }

private:
    void init()
    {
        if (m_inited) {
            return;
        }
        m_inited = true;

        bool const force = (qstrcmp(qgetenv("KWIN_FORCE_LANCZOS"), "1") == 0);
        if (force) {
            qCWarning(KWIN_CORE) << "Lanczos Filter forced on by environment variable";
        }

        if (!GLFramebuffer::supported()) {
            return;
        }

        auto gl = GLPlatform::instance();
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

        m_shader.reset(
            ShaderManager::instance()
                ->generateShaderFromFile(ShaderTrait::MapTexture,
                                         QString(),
                                         QStringLiteral(":/render/gl/shaders/lanczos.frag"))
                .release());

        if (m_shader->isValid()) {
            ShaderBinder binder(m_shader.get());
            m_uKernel = m_shader->uniformLocation("kernel");
            m_uOffsets = m_shader->uniformLocation("offsets");
        } else {
            qCDebug(KWIN_CORE) << "Shader is not valid";
            m_shader.reset();
        }
    }

    void updateOffscreenSurfaces()
    {
        auto const& space_size = m_scene->platform.base.topology.size;
        int w = space_size.width();
        int h = space_size.height();

        if (!m_offscreenTex || m_offscreenTex->width() != w || m_offscreenTex->height() != h) {
            if (m_offscreenTex) {
                delete m_offscreenTex;
                delete m_offscreenTarget;
            }
            m_offscreenTex = new GLTexture(GL_RGBA8, w, h);
            m_offscreenTex->setFilter(GL_LINEAR);
            m_offscreenTex->setWrapMode(GL_CLAMP_TO_EDGE);
            m_offscreenTarget = new GLFramebuffer(m_offscreenTex);
        }
    }

    void setUniforms()
    {
        glUniform2fv(
            m_uOffsets, m_offsets.size(), reinterpret_cast<const GLfloat*>(m_offsets.data()));
        glUniform4fv(m_uKernel, m_kernel.size(), reinterpret_cast<const GLfloat*>(m_kernel.data()));
    }

    void discardCacheTexture(EffectWindow* w)
    {
        QVariant cachedTextureVariant = w->data(LanczosCacheRole);
        if (cachedTextureVariant.isValid()) {
            delete static_cast<GLTexture*>(cachedTextureVariant.value<void*>());
            w->setData(LanczosCacheRole, QVariant());
        }
    }

    void createKernel(float delta, int* size)
    {

        auto sinc = [](float x) -> float { return std::sin(x * M_PI) / (x * M_PI); };

        auto lanczos = [sinc](float x, float a) -> float {
            if (qFuzzyCompare(x + 1.0, 1.0))
                return 1.0;

            if (qAbs(x) >= a)
                return 0.0;

            return sinc(x) * sinc(x / a);
        };

        float const a = 2.0;

        // The two outermost samples always fall at points where the lanczos
        // function returns 0, so we'll skip them.
        int const sampleCount = qBound(3, qCeil(delta * a) * 2 + 1 - 2, 29);
        int const center = sampleCount / 2;
        int const kernelSize = center + 1;
        float const factor = 1.0 / delta;

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

    void createOffsets(int count, float width, Qt::Orientation direction)
    {
        m_offsets.fill(QVector2D());
        for (int i = 0; i < count; i++) {
            m_offsets[i]
                = (direction == Qt::Horizontal) ? QVector2D(i / width, 0) : QVector2D(0, i / width);
        }
    }

    GLTexture* m_offscreenTex;
    GLFramebuffer* m_offscreenTarget;
    QBasicTimer m_timer;
    bool m_inited;
    std::unique_ptr<GLShader> m_shader;
    int m_uOffsets;
    int m_uKernel;
    std::array<QVector2D, 16> m_offsets;
    std::array<QVector4D, 16> m_kernel;
    Scene* m_scene;
};

}
