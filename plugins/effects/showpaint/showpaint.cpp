/*
SPDX-FileCopyrightText: 2007 Lubos Lunak <l.lunak@kde.org>
SPDX-FileCopyrightText: 2010 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "showpaint.h"

#include <render/effect/interface/effects_handler.h>
#include <render/effect/interface/paint_data.h>
#include <render/gl/interface/shader.h>
#include <render/gl/interface/shader_manager.h>
#include <render/gl/interface/vertex_buffer.h>

#include <KLocalizedString>
#include <QAction>
#include <QPainter>

namespace KWin
{

static const qreal s_alpha = 0.2;
static const QVector<QColor>
    s_colors{Qt::red, Qt::green, Qt::blue, Qt::cyan, Qt::magenta, Qt::yellow, Qt::gray};

ShowPaintEffect::ShowPaintEffect()
{
    auto* toggleAction = new QAction(this);
    toggleAction->setObjectName(QStringLiteral("Toggle"));
    toggleAction->setText(i18n("Toggle Show Paint"));
    effects->registerGlobalShortcutAndDefault({}, toggleAction);

    connect(toggleAction, &QAction::triggered, this, &ShowPaintEffect::toggle);
}

void ShowPaintEffect::paintScreen(effect::screen_paint_data& data)
{
    m_painted = QRegion();
    effects->paintScreen(data);
    if (effects->isOpenGLCompositing()) {
        paintGL(effect::get_mvp(data));
    } else {
        // Assume QPainter compositing.
        paintQPainter();
    }
    if (++m_colorIndex == s_colors.count()) {
        m_colorIndex = 0;
    }
}

void ShowPaintEffect::paintWindow(effect::window_paint_data& data)
{
    m_painted |= data.paint.region;
    effects->paintWindow(data);
}

void ShowPaintEffect::paintGL(const QMatrix4x4& projection)
{
    GLVertexBuffer* vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setUseColor(true);
    ShaderBinder binder(ShaderTrait::UniformColor);
    binder.shader()->setUniform(GLShader::ModelViewProjectionMatrix, projection);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    QColor color = s_colors[m_colorIndex];
    color.setAlphaF(s_alpha);
    vbo->setColor(color);

    QVector<QVector2D> verts;
    verts.reserve(m_painted.rectCount() * 12);

    for (const QRect& r : m_painted) {
        verts.push_back(QVector2D(r.x() + r.width(), r.y()));
        verts.push_back(QVector2D(r.x(), r.y()));
        verts.push_back(QVector2D(r.x(), r.y() + r.height()));
        verts.push_back(QVector2D(r.x(), r.y() + r.height()));
        verts.push_back(QVector2D(r.x() + r.width(), r.y() + r.height()));
        verts.push_back(QVector2D(r.x() + r.width(), r.y()));
    }

    vbo->setVertices(verts);
    vbo->render(GL_TRIANGLES);
    glDisable(GL_BLEND);
}

void ShowPaintEffect::paintQPainter()
{
    QColor color = s_colors[m_colorIndex];
    color.setAlphaF(s_alpha);
    for (const QRect& r : m_painted) {
        effects->scenePainter()->fillRect(r, color);
    }
}

bool ShowPaintEffect::isActive() const
{
    return m_active;
}

void ShowPaintEffect::toggle()
{
    m_active = !m_active;
    effects->addRepaintFull();
}

} // namespace KWin
