/*
SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "snaphelper.h"

#include <render/effect/interface/effect_window.h>
#include <render/effect/interface/effects_handler.h>
#include <render/effect/interface/paint_data.h>
#include <render/gl/interface/shader.h>
#include <render/gl/interface/shader_manager.h>
#include <render/gl/interface/vertex_buffer.h>

#include <QPainter>

namespace KWin
{

static const int s_lineWidth = 4;
static const QColor s_lineColor = QColor(128, 128, 128, 128);

static QRegion computeDirtyRegion(const QRect& windowRect)
{
    const QMargins outlineMargins(
        s_lineWidth / 2, s_lineWidth / 2, s_lineWidth / 2, s_lineWidth / 2);

    QRegion dirtyRegion;

    const QList<EffectScreen*> screens = effects->screens();
    for (EffectScreen* screen : screens) {
        const QRect screenRect = effects->clientArea(ScreenArea, screen, 0);

        QRect screenWindowRect = windowRect;
        screenWindowRect.moveCenter(screenRect.center());

        QRect verticalBarRect(0, 0, s_lineWidth, screenRect.height());
        verticalBarRect.moveCenter(screenRect.center());
        verticalBarRect.adjust(-1, -1, 1, 1);
        dirtyRegion += verticalBarRect;

        QRect horizontalBarRect(0, 0, screenRect.width(), s_lineWidth);
        horizontalBarRect.moveCenter(screenRect.center());
        horizontalBarRect.adjust(-1, -1, 1, 1);
        dirtyRegion += horizontalBarRect;

        const QRect outlineOuterRect
            = screenWindowRect.marginsAdded(outlineMargins).adjusted(-1, -1, 1, 1);
        const QRect outlineInnerRect
            = screenWindowRect.marginsRemoved(outlineMargins).adjusted(1, 1, -1, -1);
        dirtyRegion += QRegion(outlineOuterRect) - QRegion(outlineInnerRect);
    }

    return dirtyRegion;
}

SnapHelperEffect::SnapHelperEffect()
{
    reconfigure(ReconfigureAll);

    connect(effects, &EffectsHandler::windowClosed, this, &SnapHelperEffect::slotWindowClosed);
    connect(effects,
            &EffectsHandler::windowStartUserMovedResized,
            this,
            &SnapHelperEffect::slotWindowStartUserMovedResized);
    connect(effects,
            &EffectsHandler::windowFinishUserMovedResized,
            this,
            &SnapHelperEffect::slotWindowFinishUserMovedResized);
    connect(effects,
            &EffectsHandler::windowFrameGeometryChanged,
            this,
            &SnapHelperEffect::slotWindowFrameGeometryChanged);
}

SnapHelperEffect::~SnapHelperEffect()
{
}

void SnapHelperEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags)

    m_animation.timeLine.setDuration(
        std::chrono::milliseconds(static_cast<int>(animationTime(250))));
}

void SnapHelperEffect::prePaintScreen(effect::screen_prepaint_data& data)
{
    if (m_animation.active) {
        m_animation.timeLine.advance(data.present_time);
    }

    effects->prePaintScreen(data);
}

void SnapHelperEffect::paintScreen(effect::screen_paint_data& data)
{
    effects->paintScreen(data);

    const qreal opacityFactor = m_animation.active ? m_animation.timeLine.value() : 1.0;
    const QList<EffectScreen*> screens = effects->screens();

    // Display the guide
    if (effects->isOpenGLCompositing()) {
        GLVertexBuffer* vbo = GLVertexBuffer::streamingBuffer();
        vbo->reset();
        ShaderBinder binder(ShaderTrait::UniformColor);
        binder.shader()->setUniform(GLShader::ModelViewProjectionMatrix, effect::get_mvp(data));
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        QColor color = s_lineColor;
        color.setAlphaF(color.alphaF() * opacityFactor);
        binder.shader()->setUniform(GLShader::ColorUniform::Color, color);

        glLineWidth(s_lineWidth);
        QVector<QVector2D> verts;
        verts.reserve(screens.size() * 24);
        for (EffectScreen* screen : screens) {
            const QRect rect = effects->clientArea(ScreenArea, screen, 0);
            const int midX = rect.x() + rect.width() / 2;
            const int midY = rect.y() + rect.height() / 2;
            const int halfWidth = m_geometry.width() / 2;
            const int halfHeight = m_geometry.height() / 2;

            // Center vertical line.
            verts.push_back(QVector2D(rect.x() + rect.width() / 2, rect.y()));
            verts.push_back(QVector2D(rect.x() + rect.width() / 2, rect.y() + rect.height()));

            // Center horizontal line.
            verts.push_back(QVector2D(rect.x(), rect.y() + rect.height() / 2));
            verts.push_back(QVector2D(rect.x() + rect.width(), rect.y() + rect.height() / 2));

            // Top edge of the window outline.
            verts.push_back(QVector2D(midX - halfWidth - s_lineWidth / 2, midY - halfHeight));
            verts.push_back(QVector2D(midX + halfWidth + s_lineWidth / 2, midY - halfHeight));

            // Right edge of the window outline.
            verts.push_back(QVector2D(midX + halfWidth, midY - halfHeight + s_lineWidth / 2));
            verts.push_back(QVector2D(midX + halfWidth, midY + halfHeight - s_lineWidth / 2));

            // Bottom edge of the window outline.
            verts.push_back(QVector2D(midX + halfWidth + s_lineWidth / 2, midY + halfHeight));
            verts.push_back(QVector2D(midX - halfWidth - s_lineWidth / 2, midY + halfHeight));

            // Left edge of the window outline.
            verts.push_back(QVector2D(midX - halfWidth, midY + halfHeight - s_lineWidth / 2));
            verts.push_back(QVector2D(midX - halfWidth, midY - halfHeight + s_lineWidth / 2));
        }
        vbo->setVertices(verts);
        vbo->render(GL_LINES);

        glDisable(GL_BLEND);
        glLineWidth(1.0);
    } else {
        // Assume QPainter compositing.
        QPainter* painter = effects->scenePainter();
        painter->save();
        QColor color = s_lineColor;
        color.setAlphaF(color.alphaF() * opacityFactor);
        QPen pen(color);
        pen.setWidth(s_lineWidth);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);

        for (EffectScreen* screen : screens) {
            const QRect rect = effects->clientArea(ScreenArea, screen, 0);
            // Center lines.
            painter->drawLine(
                rect.center().x(), rect.y(), rect.center().x(), rect.y() + rect.height());
            painter->drawLine(
                rect.x(), rect.center().y(), rect.x() + rect.width(), rect.center().y());

            // Window outline.
            QRect outlineRect(0, 0, m_geometry.width(), m_geometry.height());
            outlineRect.moveCenter(rect.center());
            painter->drawRect(outlineRect);
        }
        painter->restore();
    }
}

void SnapHelperEffect::postPaintScreen()
{
    if (m_animation.active) {
        effects->addRepaint(computeDirtyRegion(m_geometry));
    }

    if (m_animation.timeLine.done()) {
        m_animation.active = false;
    }

    effects->postPaintScreen();
}

void SnapHelperEffect::slotWindowClosed(EffectWindow* w)
{
    if (w != m_window) {
        return;
    }

    m_window = nullptr;

    m_animation.active = true;
    m_animation.timeLine.setDirection(TimeLine::Backward);

    if (m_animation.timeLine.done()) {
        m_animation.timeLine.reset();
    }

    effects->addRepaint(computeDirtyRegion(m_geometry));
}

void SnapHelperEffect::slotWindowStartUserMovedResized(EffectWindow* w)
{
    if (!w->isMovable()) {
        return;
    }

    m_window = w;
    m_geometry = w->frameGeometry();

    m_animation.active = true;
    m_animation.timeLine.setDirection(TimeLine::Forward);

    if (m_animation.timeLine.done()) {
        m_animation.timeLine.reset();
    }

    effects->addRepaint(computeDirtyRegion(m_geometry));
}

void SnapHelperEffect::slotWindowFinishUserMovedResized(EffectWindow* w)
{
    if (w != m_window) {
        return;
    }

    m_window = nullptr;
    m_geometry = w->frameGeometry();

    m_animation.active = true;
    m_animation.timeLine.setDirection(TimeLine::Backward);

    if (m_animation.timeLine.done()) {
        m_animation.timeLine.reset();
    }

    effects->addRepaint(computeDirtyRegion(m_geometry));
}

void SnapHelperEffect::slotWindowFrameGeometryChanged(EffectWindow* w, const QRect& old)
{
    if (w != m_window) {
        return;
    }

    m_geometry = w->frameGeometry();

    effects->addRepaint(computeDirtyRegion(old));
}

bool SnapHelperEffect::isActive() const
{
    return m_window != nullptr || m_animation.active;
}

} // namespace KWin
