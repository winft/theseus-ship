/*
SPDX-FileCopyrightText: 2012 Filip Wieladek <wattos@gmail.com>
SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "touchpoints.h"

#include <render/effect/interface/effects_handler.h>
#include <render/effect/interface/paint_data.h>
#include <render/gl/interface/shader.h>
#include <render/gl/interface/shader_manager.h>
#include <render/gl/interface/vertex_buffer.h>

#include <KConfigGroup>
#include <QAction>
#include <QPainter>
#include <cmath>

namespace KWin
{

TouchPointsEffect::TouchPointsEffect()
    : Effect()
{
}

TouchPointsEffect::~TouchPointsEffect() = default;

static const Qt::GlobalColor s_colors[] = {Qt::blue,
                                           Qt::red,
                                           Qt::green,
                                           Qt::cyan,
                                           Qt::magenta,
                                           Qt::yellow,
                                           Qt::gray,
                                           Qt::darkBlue,
                                           Qt::darkRed,
                                           Qt::darkGreen};

Qt::GlobalColor TouchPointsEffect::colorForId(quint32 id)
{
    if (auto it = m_colors.constFind(id); it != m_colors.constEnd()) {
        return it.value();
    }

    static int s_colorIndex = -1;
    s_colorIndex = (s_colorIndex + 1) % 10;
    m_colors.insert(id, s_colors[s_colorIndex]);
    return s_colors[s_colorIndex];
}

bool TouchPointsEffect::touchDown(qint32 id, const QPointF& pos, quint32 /*time*/)
{
    TouchPoint point;
    point.pos = pos;
    point.press = true;
    point.color = colorForId(id);

    m_points << point;
    m_latestPositions.insert(id, pos);

    repaint();
    return false;
}

bool TouchPointsEffect::touchMotion(qint32 id, const QPointF& pos, quint32 /*time*/)
{
    TouchPoint point;
    point.pos = pos;
    point.press = true;
    point.color = colorForId(id);

    m_points << point;
    m_latestPositions.insert(id, pos);

    repaint();
    return false;
}

bool TouchPointsEffect::touchUp(qint32 id, quint32 /*time*/)
{
    if (auto it = m_latestPositions.constFind(id); it != m_latestPositions.constEnd()) {
        TouchPoint point;
        point.pos = it.value();
        point.press = false;
        point.color = colorForId(id);
        m_points << point;
    }

    return false;
}

void TouchPointsEffect::prePaintScreen(effect::screen_prepaint_data& data)
{
    int time = 0;
    if (m_lastPresentTime.count()) {
        time = (data.present_time - m_lastPresentTime).count();
    }

    auto it = m_points.begin();
    while (it != m_points.end()) {
        it->time += time;
        if (it->time > m_ringLife) {
            it = m_points.erase(it);
        } else {
            it++;
        }
    }

    if (m_points.isEmpty()) {
        m_lastPresentTime = std::chrono::milliseconds::zero();
    } else {
        m_lastPresentTime = data.present_time;
    }

    effects->prePaintScreen(data);
}

void TouchPointsEffect::paintScreen(effect::screen_paint_data& data)
{
    effects->paintScreen(data);

    paintScreenSetup(data);
    for (auto it = m_points.constBegin(), end = m_points.constEnd(); it != end; ++it) {
        for (int i = 0; i < m_ringCount; ++i) {
            float alpha = computeAlpha(it->time, i);
            float size = computeRadius(it->time, it->press, i);
            if (size > 0 && alpha > 0) {
                QColor color = it->color;
                color.setAlphaF(alpha);
                drawCircle(color, it->pos.x(), it->pos.y(), size);
            }
        }
    }
    paintScreenFinish(data);
}

void TouchPointsEffect::postPaintScreen()
{
    effects->postPaintScreen();
    repaint();
}

float TouchPointsEffect::computeRadius(int time, bool press, int ring)
{
    float ringDistance = m_ringLife / (m_ringCount * 3);
    if (press) {
        return ((time - ringDistance * ring) / m_ringLife) * m_ringMaxSize;
    }
    return ((m_ringLife - time - ringDistance * ring) / m_ringLife) * m_ringMaxSize;
}

float TouchPointsEffect::computeAlpha(int time, int ring)
{
    float ringDistance = m_ringLife / (m_ringCount * 3);
    return (m_ringLife - static_cast<float>(time) - ringDistance * (ring)) / m_ringLife;
}

void TouchPointsEffect::repaint()
{
    if (!m_points.isEmpty()) {
        QRegion dirtyRegion;
        const int radius = m_ringMaxSize + m_lineWidth;
        for (auto it = m_points.constBegin(), end = m_points.constEnd(); it != end; ++it) {
            dirtyRegion
                |= QRect(it->pos.x() - radius, it->pos.y() - radius, 2 * radius, 2 * radius);
        }
        effects->addRepaint(dirtyRegion);
    }
}

bool TouchPointsEffect::isActive() const
{
    return !m_points.isEmpty();
}

void TouchPointsEffect::drawCircle(const QColor& color, float cx, float cy, float r)
{
    if (effects->isOpenGLCompositing()) {
        drawCircleGl(color, cx, cy, r);
    } else {
        drawCircleQPainter(color, cx, cy, r);
    }
}

void TouchPointsEffect::paintScreenSetup(effect::screen_paint_data const& data)
{
    if (effects->isOpenGLCompositing()) {
        paintScreenSetupGl(data);
    }
}

void TouchPointsEffect::paintScreenFinish(effect::screen_paint_data const& data)
{
    if (effects->isOpenGLCompositing()) {
        paintScreenFinishGl(data);
    }
}

void TouchPointsEffect::drawCircleGl(const QColor& color, float cx, float cy, float r)
{
    static int const num_segments = 80;
    static float const theta = 2 * 3.1415926 / float(num_segments);
    static float const c = cosf(theta); // precalculate the sine and cosine
    static float const s = sinf(theta);
    float t;

    // we start at angle = 0
    auto x = r;
    auto y = 0.f;

    auto vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    ShaderManager::instance()->getBoundShader()->setUniform(GLShader::ColorUniform::Color, color);
    QVector<QVector2D> verts;
    verts.reserve(num_segments);

    for (int ii = 0; ii < num_segments; ++ii) {
        // output vertex
        verts.push_back(QVector2D(x + cx, y + cy));

        // apply the rotation matrix
        t = x;
        x = c * x - s * y;
        y = s * t + c * y;
    }
    vbo->setVertices(verts);
    vbo->render(GL_LINE_LOOP);
}

void TouchPointsEffect::drawCircleQPainter(const QColor& color, float cx, float cy, float r)
{
    auto painter = effects->scenePainter();
    painter->save();
    painter->setPen(color);
    painter->drawArc(cx - r, cy - r, r * 2, r * 2, 0, 5760);
    painter->restore();
}

void TouchPointsEffect::paintScreenSetupGl(effect::screen_paint_data const& data)
{
    auto shader = ShaderManager::instance()->pushShader(ShaderTrait::UniformColor);
    shader->setUniform(GLShader::ModelViewProjectionMatrix, effect::get_mvp(data));

    glLineWidth(m_lineWidth);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void TouchPointsEffect::paintScreenFinishGl(effect::screen_paint_data const& /*data*/)
{
    glDisable(GL_BLEND);

    ShaderManager::instance()->popShader();
}

} // namespace
