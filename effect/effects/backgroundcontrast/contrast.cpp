/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2011 Philipp Knechtges <philipp-dev@knechtges.com>
    SPDX-FileCopyrightText: 2014 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "contrast.h"
#include "contrastshader.h"

#include <kwineffects/effect_window.h>
#include <kwineffects/effects_handler.h>
#include <kwineffects/paint_data.h>

#include <QMatrix4x4>

namespace KWin
{

void update_function(ContrastEffect& effect, KWin::effect::color_update const& update)
{
    if (!update.base.window) {
        // Reset requested
        effect.reset();
        return;
    }

    if (update.base.valid) {
        effect.m_windowData[update.base.window] = {
            .colorMatrix = update.color,
            .contrastRegion = update.region,
        };
    } else {
        effect.m_windowData.remove(update.base.window);
    }
}

ContrastEffect::ContrastEffect()
{
    shader = std::make_unique<ContrastShader>();

    reconfigure(ReconfigureAll);

    if (shader && shader->isValid()) {
        auto& contrast_integration = effects->get_contrast_integration();
        auto update = [this](auto&& data) { update_function(*this, data); };
        contrast_integration.add(*this, update);
    }

    connect(effects, &EffectsHandler::windowDeleted, this, &ContrastEffect::slotWindowDeleted);
}

ContrastEffect::~ContrastEffect()
{
}

void ContrastEffect::reset()
{
    if (!supported()) {
        effects->makeOpenGLContextCurrent();
        effects->reloadEffect(this);
        // TODO(romangg): done context?
    }
}

void ContrastEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags)

    if (shader)
        shader->init();

    if (!shader || !shader->isValid()) {
        effects->get_contrast_integration().remove(*this);
    }
}

void ContrastEffect::slotWindowDeleted(EffectWindow* w)
{
    m_windowData.remove(w);
}

bool ContrastEffect::enabledByDefault()
{
    GLPlatform* gl = GLPlatform::instance();

    if (gl->isIntel() && gl->chipClass() < SandyBridge)
        return false;
    if (gl->isPanfrost() && gl->chipClass() <= MaliT8XX)
        return false;
    if (gl->isSoftwareEmulation()) {
        return false;
    }

    return true;
}

bool ContrastEffect::supported()
{
    bool supported = effects->isOpenGLCompositing() && GLRenderTarget::supported();

    if (supported) {
        int maxTexSize;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);

        const QSize screenSize = effects->virtualScreenSize();
        if (screenSize.width() > maxTexSize || screenSize.height() > maxTexSize)
            supported = false;
    }
    return supported;
}

QRegion ContrastEffect::contrastRegion(const EffectWindow* w) const
{
    QRegion region;

    if (auto const it = m_windowData.find(w); it != m_windowData.end()) {
        auto const& appRegion = it->contrastRegion;
        if (!appRegion.isEmpty()) {
            region |= appRegion.translated(w->contentsRect().topLeft()) & w->decorationInnerRect();
        } else {
            // An empty region means that the blur effect should be enabled
            // for the whole window.
            region = w->decorationInnerRect();
        }
    }

    return region;
}

void ContrastEffect::uploadRegion(QVector2D*& map, const QRegion& region)
{
    for (const QRect& r : region) {
        const QVector2D topLeft(r.x(), r.y());
        const QVector2D topRight(r.x() + r.width(), r.y());
        const QVector2D bottomLeft(r.x(), r.y() + r.height());
        const QVector2D bottomRight(r.x() + r.width(), r.y() + r.height());

        // First triangle
        *(map++) = topRight;
        *(map++) = topLeft;
        *(map++) = bottomLeft;

        // Second triangle
        *(map++) = bottomLeft;
        *(map++) = bottomRight;
        *(map++) = topRight;
    }
}

void ContrastEffect::uploadGeometry(GLVertexBuffer* vbo, const QRegion& region)
{
    const int vertexCount = region.rectCount() * 6;
    if (!vertexCount)
        return;

    QVector2D* map = static_cast<QVector2D*>(vbo->map(vertexCount * sizeof(QVector2D)));
    uploadRegion(map, region);
    vbo->unmap();

    const GLVertexAttrib layout[] = {{VA_Position, 2, GL_FLOAT, 0}, {VA_TexCoord, 2, GL_FLOAT, 0}};

    vbo->setAttribLayout(layout, 2, sizeof(QVector2D));
}

bool ContrastEffect::shouldContrast(const EffectWindow* w,
                                    int mask,
                                    const WindowPaintData& data) const
{
    if (!shader || !shader->isValid())
        return false;

    if (effects->activeFullScreenEffect() && !w->data(WindowForceBackgroundContrastRole).toBool())
        return false;

    if (w->isDesktop())
        return false;

    bool scaled = !qFuzzyCompare(data.xScale(), 1.0) && !qFuzzyCompare(data.yScale(), 1.0);
    bool translated = data.xTranslation() || data.yTranslation();

    if ((scaled || (translated || (mask & PAINT_WINDOW_TRANSFORMED)))
        && !w->data(WindowForceBackgroundContrastRole).toBool())
        return false;

    return true;
}

void ContrastEffect::drawWindow(EffectWindow* w,
                                int mask,
                                const QRegion& region,
                                WindowPaintData& data)
{
    if (shouldContrast(w, mask, data)) {
        auto const screen = effects->renderTargetRect();
        QRegion shape = region & contrastRegion(w).translated(w->pos()) & screen;

        // let's do the evil parts - someone wants to blur behind a transformed window
        const bool translated = data.xTranslation() || data.yTranslation();
        const bool scaled = data.xScale() != 1 || data.yScale() != 1;
        if (scaled) {
            QPoint pt = shape.boundingRect().topLeft();
            QRegion scaledShape;
            for (QRect r : shape) {
                r.moveTo(pt.x() + (r.x() - pt.x()) * data.xScale() + data.xTranslation(),
                         pt.y() + (r.y() - pt.y()) * data.yScale() + data.yTranslation());
                r.setWidth(r.width() * data.xScale());
                r.setHeight(r.height() * data.yScale());
                scaledShape |= r;
            }
            shape = scaledShape & region;

            // Only translated, not scaled
        } else if (translated) {
            shape = shape.translated(data.xTranslation(), data.yTranslation());
            shape = shape & region;
        }

        if (!shape.isEmpty()) {
            doContrast(w, shape, screen, data.opacity(), data.screenProjectionMatrix());
        }
    }

    // Draw the window over the contrast area
    effects->drawWindow(w, mask, region, data);
}

void ContrastEffect::doContrast(EffectWindow* w,
                                const QRegion& shape,
                                const QRect& screen,
                                const float opacity,
                                const QMatrix4x4& screenProjection)
{
    const QRegion actualShape = shape & screen;
    const QRect r = actualShape.boundingRect();

    auto const scale = effects->renderTargetScale();

    // Upload geometry for the horizontal and vertical passes
    GLVertexBuffer* vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    uploadGeometry(vbo, actualShape);
    vbo->bindArrays();

    // Create a scratch texture and copy the area in the back buffer that we're
    // going to blur into it
    GLTexture scratch(GL_RGBA8, r.width() * scale, r.height() * scale);
    scratch.setFilter(GL_LINEAR);
    scratch.setWrapMode(GL_CLAMP_TO_EDGE);
    scratch.bind();

    auto const sg = effects->renderTargetRect();
    glCopyTexSubImage2D(GL_TEXTURE_2D,
                        0,
                        0,
                        0,
                        (r.x() - sg.x()) * scale,
                        (sg.height() - (r.y() - sg.y() + r.height())) * scale,
                        scratch.width(),
                        scratch.height());

    // Draw the texture on the offscreen framebuffer object, while blurring it horizontally

    shader->setColorMatrix(m_windowData.value(w).colorMatrix);
    shader->bind();

    shader->setOpacity(opacity);
    // Set up the texture matrix to transform from screen coordinates
    // to texture coordinates.
    QMatrix4x4 textureMatrix;
    textureMatrix.scale(1.0 / r.width(), -1.0 / r.height(), 1);
    textureMatrix.translate(-r.x(), -r.height() - r.y(), 0);
    shader->setTextureMatrix(textureMatrix);
    shader->setModelViewProjectionMatrix(screenProjection);

    vbo->draw(GL_TRIANGLES, 0, actualShape.rectCount() * 6);

    scratch.unbind();
    scratch.discard();

    vbo->unbindArrays();

    if (opacity < 1.0) {
        glDisable(GL_BLEND);
    }

    shader->unbind();
}

bool ContrastEffect::isActive() const
{
    return !effects->isScreenLocked();
}

} // namespace KWin
