/*
    SPDX-FileCopyrightText: 2010 Fredrik Höglund <fredrik@kde.org>
    SPDX-FileCopyrightText: 2011 Philipp Knechtges <philipp-dev@knechtges.com>
    SPDX-FileCopyrightText: 2014 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "contrast.h"
#include "contrastshader.h"

#include <render/effect/interface/effect_window.h>
#include <render/effect/interface/effects_handler.h>
#include <render/effect/interface/paint_data.h>

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

    auto& win_data = effect.m_windowData;
    if (update.base.valid) {
        win_data.try_emplace(update.base.window);
        win_data.at(update.base.window) = {
            .colorMatrix = update.color,
            .contrastRegion = update.region,
        };
    } else {
        effects->makeOpenGLContextCurrent();
        win_data.erase(update.base.window);
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
    if (auto it = m_windowData.find(w); it != m_windowData.end()) {
        effects->makeOpenGLContextCurrent();
        m_windowData.erase(it);
    }
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
        auto const& appRegion = it->second.contrastRegion;
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

void ContrastEffect::uploadRegion(std::span<QVector2D> map, const QRegion& region)
{
    size_t index = 0;

    for (const QRect& r : region) {
        QVector2D const topLeft(r.x(), r.y());
        QVector2D const topRight(r.x() + r.width(), r.y());
        QVector2D const bottomLeft(r.x(), r.y() + r.height());
        QVector2D const bottomRight(r.x() + r.width(), r.y() + r.height());

        // First triangle
        map[index++] = topRight;
        map[index++] = topLeft;
        map[index++] = bottomLeft;

        // Second triangle
        map[index++] = bottomLeft;
        map[index++] = bottomRight;
        map[index++] = topRight;
    }
}

void ContrastEffect::uploadGeometry(GLVertexBuffer* vbo, const QRegion& region)
{
    auto const vertexCount = region.rectCount() * 6;
    if (!vertexCount) {
        return;
    }

    auto const map = vbo->map<QVector2D>(vertexCount);
    uploadRegion(*map, region);
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

    auto const screen = data.render.viewport;
    auto shape
        = data.paint.region & contrastRegion(&data.window).translated(data.window.pos()) & screen;

    // let's do the evil parts - someone wants to blur behind a transformed window
    if (!qFuzzyCompare(data.paint.geo.scale.x(), 1.f)
        || !qFuzzyCompare(data.paint.geo.scale.y(), 1.f)) {
        QPoint pt = shape.boundingRect().topLeft();
        QRegion scaledShape;
        for (auto const& r : shape) {
            QPointF const topLeft(pt.x() + (r.x() - pt.x()) * data.paint.geo.scale.x()
                                      + data.paint.geo.translation.x(),
                                  pt.y() + (r.y() - pt.y()) * data.paint.geo.scale.y()
                                      + data.paint.geo.translation.y());
            QPoint const bottomRight(
                std::floor(topLeft.x() + r.width() * data.paint.geo.scale.x()) - 1,
                std::floor(topLeft.y() + r.height() * data.paint.geo.scale.y()) - 1);
            scaledShape
                |= QRect(QPoint(std::floor(topLeft.x()), std::floor(topLeft.y())), bottomRight);
        }
        shape = scaledShape & data.paint.region;
    } else if (data.paint.geo.translation.x() || data.paint.geo.translation.y()) {
        // Only translated, not scaled
        QRegion translated;
        for (auto const& r : shape) {
            const QRectF t = QRectF(r).translated(data.paint.geo.translation.x(),
                                                  data.paint.geo.translation.y());
            const QPoint topLeft(std::ceil(t.x()), std::ceil(t.y()));
            const QPoint bottomRight(std::floor(t.x() + t.width() - 1),
                                     std::floor(t.y() + t.height() - 1));
            translated |= QRect(topLeft, bottomRight);
        }
        shape = translated & data.paint.region;
    }

    if (!shape.isEmpty()) {
        doContrast(data, shape & screen);
    }

    // Draw the window over the contrast area
    effects->drawWindow(data);
}

void ContrastEffect::doContrast(effect::window_paint_data& data, QRegion const& shape)
{
    auto mvp = effect::get_mvp(data);
    auto const rect = effect::map_to_viewport(data.render, shape.boundingRect());

    // Upload geometry for the horizontal and vertical passes
    auto vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    uploadGeometry(vbo, shape);
    vbo->bindArrays();

    assert(m_windowData.contains(&data.window));
    auto& win_data = m_windowData.at(&data.window);
    auto& texture = win_data.texture;

    if (!texture || texture->size() != rect.size()) {
        texture = std::make_unique<GLTexture>(GL_RGBA8, rect.size());
        win_data.fbo = std::make_unique<GLFramebuffer>(texture.get());
        texture->setFilter(GL_LINEAR);
        texture->setWrapMode(GL_CLAMP_TO_EDGE);
    }

    texture->bind();

    win_data.fbo->blit_from_current_render_target(
        data.render, shape.boundingRect(), QRect({}, texture->size()));

    // Draw the texture on the offscreen framebuffer object, while blurring it horizontally

    shader->setColorMatrix(win_data.colorMatrix);
    shader->bind();

    shader->setOpacity(data.paint.opacity);

    // Set up the texture matrix to transform from screen coordinates to texture coordinates.
    QMatrix4x4 textureMatrix;
    textureMatrix.scale(1.0 / rect.width(), 1.0 / rect.height(), 1);
    textureMatrix.translate(-rect.x(), -rect.y(), 0);

    shader->setTextureMatrix(textureMatrix);
    shader->setModelViewProjectionMatrix(mvp);

    vbo->draw(GL_TRIANGLES, 0, shape.rectCount() * 6);

    texture->unbind();
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
