/*
SPDX-FileCopyrightText: 2007 Lubos Lunak <l.lunak@kde.org>
SPDX-FileCopyrightText: 2007 Christian Nitschkowski <christian.nitschkowski@kdemail.net>
SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "magnifier.h"

// KConfigSkeleton
#include "magnifierconfig.h"

#include <render/effect/interface/effect_window.h>
#include <render/effect/interface/effects_handler.h>
#include <render/effect/interface/paint_data.h>
#include <render/gl/interface/framebuffer.h>
#include <render/gl/interface/shader.h>
#include <render/gl/interface/shader_manager.h>
#include <render/gl/interface/texture.h>
#include <render/gl/interface/vertex_buffer.h>

#include <KStandardAction>
#include <QAction>

namespace KWin
{

const int FRAME_WIDTH = 5;

MagnifierEffect::MagnifierEffect()
    : m_zoom(1)
    , m_targetZoom(1)
    , m_polling(false)
    , m_lastPresentTime(std::chrono::milliseconds::zero())
    , m_texture(nullptr)
    , m_fbo(nullptr)
{
    initConfig<MagnifierConfig>();
    QAction* a;
    a = KStandardAction::zoomIn(this, &MagnifierEffect::zoomIn, this);
    effects->registerGlobalShortcutAndDefault({static_cast<Qt::Key>(Qt::META) + Qt::Key_Equal}, a);

    a = KStandardAction::zoomOut(this, &MagnifierEffect::zoomOut, this);
    effects->registerGlobalShortcutAndDefault({static_cast<Qt::Key>(Qt::META) + Qt::Key_Minus}, a);

    a = KStandardAction::actualSize(this, &MagnifierEffect::toggle, this);
    effects->registerGlobalShortcutAndDefault({static_cast<Qt::Key>(Qt::META) + Qt::Key_0}, a);

    connect(effects, &EffectsHandler::mouseChanged, this, &MagnifierEffect::slotMouseChanged);
    connect(effects, &EffectsHandler::windowDamaged, this, &MagnifierEffect::slotWindowDamaged);

    reconfigure(ReconfigureAll);
}

MagnifierEffect::~MagnifierEffect()
{
    // Save the zoom value.
    MagnifierConfig::setInitialZoom(m_targetZoom);
    MagnifierConfig::self()->save();
}

bool MagnifierEffect::supported()
{
    return effects->isOpenGLCompositing() && GLFramebuffer::blitSupported();
}

void MagnifierEffect::reconfigure(ReconfigureFlags)
{
    MagnifierConfig::self()->read();
    int width, height;
    width = MagnifierConfig::width();
    height = MagnifierConfig::height();
    m_magnifierSize = QSize(width, height);
    // Load the saved zoom value.
    m_targetZoom = MagnifierConfig::initialZoom();
    if (m_targetZoom != m_zoom)
        toggle();
}

void MagnifierEffect::prePaintScreen(effect::screen_prepaint_data& data)
{
    const int time
        = m_lastPresentTime.count() ? (data.present_time - m_lastPresentTime).count() : 0;

    if (m_zoom != m_targetZoom) {
        double diff = time / animationTime(500.0);
        if (m_targetZoom > m_zoom)
            m_zoom = qMin(m_zoom * qMax(1 + diff, 1.2), m_targetZoom);
        else {
            m_zoom = qMax(m_zoom * qMin(1 - diff, 0.8), m_targetZoom);
            if (m_zoom == 1.0) {
                // m_zoom ended - delete FBO and texture
                m_fbo.reset();
                m_texture.reset();
            }
        }
    }

    if (m_zoom != m_targetZoom) {
        m_lastPresentTime = data.present_time;
    } else {
        m_lastPresentTime = std::chrono::milliseconds::zero();
    }

    effects->prePaintScreen(data);
    if (m_zoom != 1.0)
        data.paint.region
            |= magnifierArea().adjusted(-FRAME_WIDTH, -FRAME_WIDTH, FRAME_WIDTH, FRAME_WIDTH);
}

void MagnifierEffect::paintScreen(effect::screen_paint_data& data)
{
    effects->paintScreen(data);

    if (m_zoom == 1.0 || !effects->isOpenGLCompositing()) {
        return;
    }

    // get the right area from the current rendered screen
    auto const area = magnifierArea();
    auto const cursor = cursorPos();

    QRect srcArea(cursor.x() - static_cast<double>(area.width()) / (m_zoom * 2),
                  cursor.y() - static_cast<double>(area.height()) / (m_zoom * 2),
                  static_cast<double>(area.width()) / m_zoom,
                  static_cast<double>(area.height()) / m_zoom);

    m_fbo->blit_from_current_render_target(data.render, srcArea, QRect(QPoint(), m_fbo->size()));

    // paint magnifier
    m_texture->bind();

    auto s = ShaderManager::instance()->pushShader(ShaderTrait::MapTexture);
    auto const size = effects->virtualScreenSize();

    QMatrix4x4 mvp;
    mvp.ortho(0, size.width(), size.height(), 0, 0, 65535);
    mvp.translate(area.x(), area.y());

    s->setUniform(GLShader::ModelViewProjectionMatrix, mvp);
    m_texture->render(area.size());
    ShaderManager::instance()->popShader();
    m_texture->unbind();

    QVector<float> verts;
    auto vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setColor(QColor(0, 0, 0));
    QRectF const areaF = area;

    // top frame
    verts << areaF.right() + FRAME_WIDTH << areaF.top() - FRAME_WIDTH;
    verts << areaF.left() - FRAME_WIDTH << areaF.top() - FRAME_WIDTH;
    verts << areaF.left() - FRAME_WIDTH << areaF.top();
    verts << areaF.left() - FRAME_WIDTH << areaF.top();
    verts << areaF.right() + FRAME_WIDTH << areaF.top();
    verts << areaF.right() + FRAME_WIDTH << areaF.top() - FRAME_WIDTH;
    // left frame
    verts << areaF.left() << areaF.top() - FRAME_WIDTH;
    verts << areaF.left() - FRAME_WIDTH << areaF.top() - FRAME_WIDTH;
    verts << areaF.left() - FRAME_WIDTH << areaF.bottom() + FRAME_WIDTH;
    verts << areaF.left() - FRAME_WIDTH << areaF.bottom() + FRAME_WIDTH;
    verts << areaF.left() << areaF.bottom() + FRAME_WIDTH;
    verts << areaF.left() << areaF.top() - FRAME_WIDTH;
    // right frame
    verts << areaF.right() + FRAME_WIDTH << areaF.top() - FRAME_WIDTH;
    verts << areaF.right() << areaF.top() - FRAME_WIDTH;
    verts << areaF.right() << areaF.bottom() + FRAME_WIDTH;
    verts << areaF.right() << areaF.bottom() + FRAME_WIDTH;
    verts << areaF.right() + FRAME_WIDTH << areaF.bottom() + FRAME_WIDTH;
    verts << areaF.right() + FRAME_WIDTH << areaF.top() - FRAME_WIDTH;
    // bottom frame
    verts << areaF.right() + FRAME_WIDTH << areaF.bottom();
    verts << areaF.left() - FRAME_WIDTH << areaF.bottom();
    verts << areaF.left() - FRAME_WIDTH << areaF.bottom() + FRAME_WIDTH;
    verts << areaF.left() - FRAME_WIDTH << areaF.bottom() + FRAME_WIDTH;
    verts << areaF.right() + FRAME_WIDTH << areaF.bottom() + FRAME_WIDTH;
    verts << areaF.right() + FRAME_WIDTH << areaF.bottom();
    vbo->setData(verts.size() / 2, 2, verts.constData(), nullptr);

    ShaderBinder binder(ShaderTrait::UniformColor);
    binder.shader()->setUniform(GLShader::ModelViewProjectionMatrix, effect::get_mvp(data));
    vbo->render(GL_TRIANGLES);
}

void MagnifierEffect::postPaintScreen()
{
    if (m_zoom != m_targetZoom) {
        QRect framedarea
            = magnifierArea().adjusted(-FRAME_WIDTH, -FRAME_WIDTH, FRAME_WIDTH, FRAME_WIDTH);
        effects->addRepaint(framedarea);
    }
    effects->postPaintScreen();
}

QRect MagnifierEffect::magnifierArea(QPoint pos) const
{
    return QRect(pos.x() - m_magnifierSize.width() / 2,
                 pos.y() - m_magnifierSize.height() / 2,
                 m_magnifierSize.width(),
                 m_magnifierSize.height());
}

void MagnifierEffect::zoomIn()
{
    m_targetZoom *= 1.2;
    if (!m_polling) {
        m_polling = true;
        effects->startMousePolling();
    }
    if (effects->isOpenGLCompositing() && !m_texture) {
        effects->makeOpenGLContextCurrent();
        m_texture = std::make_unique<GLTexture>(
            GL_RGBA8, m_magnifierSize.width(), m_magnifierSize.height());
        m_texture->set_content_transform(effect::transform_type::normal);
        m_fbo = std::make_unique<GLFramebuffer>(m_texture.get());
    }
    effects->addRepaint(
        magnifierArea().adjusted(-FRAME_WIDTH, -FRAME_WIDTH, FRAME_WIDTH, FRAME_WIDTH));
}

void MagnifierEffect::zoomOut()
{
    m_targetZoom /= 1.2;
    if (m_targetZoom <= 1) {
        m_targetZoom = 1;
        if (m_polling) {
            m_polling = false;
            effects->stopMousePolling();
        }
        if (m_zoom == m_targetZoom) {
            effects->makeOpenGLContextCurrent();
            m_fbo.reset();
            m_texture.reset();
        }
    }
    effects->addRepaint(
        magnifierArea().adjusted(-FRAME_WIDTH, -FRAME_WIDTH, FRAME_WIDTH, FRAME_WIDTH));
}

void MagnifierEffect::toggle()
{
    if (m_zoom == 1.0) {
        if (m_targetZoom == 1.0) {
            m_targetZoom = 2;
        }
        if (!m_polling) {
            m_polling = true;
            effects->startMousePolling();
        }
        if (effects->isOpenGLCompositing() && !m_texture) {
            effects->makeOpenGLContextCurrent();
            m_texture = std::make_unique<GLTexture>(
                GL_RGBA8, m_magnifierSize.width(), m_magnifierSize.height());
            m_texture->set_content_transform(effect::transform_type::normal);
            m_fbo = std::make_unique<GLFramebuffer>(m_texture.get());
        }
    } else {
        m_targetZoom = 1;
        if (m_polling) {
            m_polling = false;
            effects->stopMousePolling();
        }
    }
    effects->addRepaint(
        magnifierArea().adjusted(-FRAME_WIDTH, -FRAME_WIDTH, FRAME_WIDTH, FRAME_WIDTH));
}

void MagnifierEffect::slotMouseChanged(const QPoint& pos,
                                       const QPoint& old,
                                       Qt::MouseButtons,
                                       Qt::MouseButtons,
                                       Qt::KeyboardModifiers,
                                       Qt::KeyboardModifiers)
{
    if (pos != old && m_zoom != 1)
        // need full repaint as we might lose some change events on fast mouse movements
        // see Bug 187658
        effects->addRepaintFull();
}

void MagnifierEffect::slotWindowDamaged()
{
    if (isActive()) {
        effects->addRepaint(magnifierArea());
    }
}

bool MagnifierEffect::isActive() const
{
    return m_zoom != 1.0 || m_zoom != m_targetZoom;
}

QSize MagnifierEffect::magnifierSize() const
{
    return m_magnifierSize;
}

qreal MagnifierEffect::targetZoom() const
{
    return m_targetZoom;
}

} // namespace
