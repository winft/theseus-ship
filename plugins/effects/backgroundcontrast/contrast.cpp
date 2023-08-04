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
    if (gl->isLima() || gl->isVideoCore4() || gl->isVideoCore3D()) {
        return false;
    }
    if (gl->isSoftwareEmulation()) {
        return false;
    }

    return true;
}

bool ContrastEffect::supported()
{
    bool supported = effects->isOpenGLCompositing() && GLFramebuffer::supported();

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
        QVector2D const topLeft(r.x(), r.y());
        QVector2D const topRight(r.x() + r.width(), r.y());
        QVector2D const bottomLeft(r.x(), r.y() + r.height());
        QVector2D const bottomRight(r.x() + r.width(), r.y() + r.height());

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
    auto const vertexCount = region.rectCount() * 6;
    if (!vertexCount) {
        return;
    }

    auto map = static_cast<QVector2D*>(vbo->map(vertexCount * sizeof(QVector2D)));
    uploadRegion(map, region);
    vbo->unmap();

    GLVertexAttrib const layout[] = {{VA_Position, 2, GL_FLOAT, 0}, {VA_TexCoord, 2, GL_FLOAT, 0}};

    vbo->setAttribLayout(layout, 2, sizeof(QVector2D));
}

bool ContrastEffect::shouldContrast(effect::window_paint_data const& data) const
{
    if (!shader || !shader->isValid()) {
        return false;
    }
    if (effects->activeFullScreenEffect()
        && !data.window.data(WindowForceBackgroundContrastRole).toBool()) {
        return false;
    }
    if (data.window.isDesktop()) {
        return false;
    }

    auto const scaled = !qFuzzyCompare(data.paint.geo.scale.x(), float(1.0))
        || !qFuzzyCompare(data.paint.geo.scale.y(), float(1.0));
    auto const translated = data.paint.geo.translation.x() || data.paint.geo.translation.y();

    if ((scaled || (translated || (data.paint.mask & PAINT_WINDOW_TRANSFORMED)))
        && !data.window.data(WindowForceBackgroundContrastRole).toBool()) {
        return false;
    }

    return true;
}

void ContrastEffect::drawWindow(effect::window_paint_data& data)
{
    if (!shouldContrast(data)) {
        effects->drawWindow(data);
        return;
    }

    auto const screen = effects->renderTargetRect();
    auto shape
        = data.paint.region & contrastRegion(&data.window).translated(data.window.pos()) & screen;

    // let's do the evil parts - someone wants to blur behind a transformed window
    if (!qFuzzyCompare(data.paint.geo.scale.x(), 1.f)
        || !qFuzzyCompare(data.paint.geo.scale.y(), 1.f)) {
        QPoint pt = shape.boundingRect().topLeft();
        QRegion scaledShape;
        for (QRect r : shape) {
            r.moveTo(pt.x() + (r.x() - pt.x()) * data.paint.geo.scale.x()
                         + data.paint.geo.translation.x(),
                     pt.y() + (r.y() - pt.y()) * data.paint.geo.scale.y()
                         + data.paint.geo.translation.y());
            r.setWidth(r.width() * data.paint.geo.scale.x());
            r.setHeight(r.height() * data.paint.geo.scale.y());
            scaledShape |= r;
        }
        shape = scaledShape & data.paint.region;
    } else if (data.paint.geo.translation.x() || data.paint.geo.translation.y()) {
        // Only translated, not scaled
        shape = shape.translated(data.paint.geo.translation.x(), data.paint.geo.translation.y());
        shape = shape & data.paint.region;
    }

    if (!shape.isEmpty()) {
        doContrast(data, shape, screen);
    }

    // Draw the window over the contrast area
    effects->drawWindow(data);
}

void ContrastEffect::doContrast(effect::window_paint_data const& data,
                                QRegion const& shape,
                                QRect const& screen)
{
    auto const actualShape = shape & screen;
    auto const r = actualShape.boundingRect();

    auto const scale = effects->renderTargetScale();

    // Upload geometry for the horizontal and vertical passes
    auto vbo = GLVertexBuffer::streamingBuffer();
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

    shader->setColorMatrix(m_windowData.value(&data.window).colorMatrix);
    shader->bind();

    shader->setOpacity(data.paint.opacity);

    // Set up the texture matrix to transform from screen coordinates
    // to texture coordinates.
    QMatrix4x4 textureMatrix;
    textureMatrix.scale(1.0 / r.width(), -1.0 / r.height(), 1);
    textureMatrix.translate(-r.x(), -r.height() - r.y(), 0);
    shader->setTextureMatrix(textureMatrix);
    shader->setModelViewProjectionMatrix(data.paint.screen_projection_matrix);

    vbo->draw(GL_TRIANGLES, 0, actualShape.rectCount() * 6);

    scratch.unbind();
    scratch.discard();

    vbo->unbindArrays();

    if (data.paint.opacity < 1.0) {
        glDisable(GL_BLEND);
    }

    shader->unbind();
}

bool ContrastEffect::isActive() const
{
    return !effects->isScreenLocked();
}

} // namespace KWin
