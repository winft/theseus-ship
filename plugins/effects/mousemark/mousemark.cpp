/*
SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
SPDX-FileCopyrightText: 2007 Christian Nitschkowski <christian.nitschkowski@kdemail.net>
SPDX-FileCopyrightText: 2023 Andrew Shark <ashark at linuxcomp.ru>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "mousemark.h"

// KConfigSkeleton
#include "mousemarkconfig.h"

#include <render/effect/interface/effects_handler.h>
#include <render/effect/interface/paint_data.h>
#include <render/gl/interface/platform.h>
#include <render/gl/interface/shader.h>
#include <render/gl/interface/shader_manager.h>
#include <render/gl/interface/vertex_buffer.h>

#include <KLocalizedString>
#include <QAction>
#include <QLoggingCategory>
#include <QPainter>

#include <cmath>

Q_LOGGING_CATEGORY(KWIN_MOUSEMARK, "kwin_effect_mousemark", QtWarningMsg)

namespace KWin
{

static consteval QPoint nullPoint()
{
    return QPoint(-1, -1);
}

MouseMarkEffect::MouseMarkEffect()
{
    MouseMarkConfig::instance(effects->config());

    QAction* a = new QAction(this);
    a->setObjectName(QStringLiteral("ClearMouseMarks"));
    a->setText(i18n("Clear All Mouse Marks"));
    effects->registerGlobalShortcutAndDefault({Qt::SHIFT | Qt::META | Qt::Key_F11}, a);
    connect(a, &QAction::triggered, this, &MouseMarkEffect::clear);

    a = new QAction(this);
    a->setObjectName(QStringLiteral("ClearLastMouseMark"));
    a->setText(i18n("Clear Last Mouse Mark"));
    effects->registerGlobalShortcutAndDefault({Qt::SHIFT | Qt::META | Qt::Key_F12}, a);
    connect(a, &QAction::triggered, this, &MouseMarkEffect::clearLast);

    connect(effects, &EffectsHandler::mouseChanged, this, &MouseMarkEffect::slotMouseChanged);
    connect(effects,
            &EffectsHandler::screenLockingChanged,
            this,
            &MouseMarkEffect::screenLockingChanged);
    reconfigure(ReconfigureAll);
    arrow_tail = nullPoint();
    effects->startMousePolling(); // We require it to detect activation as well
}

MouseMarkEffect::~MouseMarkEffect()
{
    effects->stopMousePolling();
}

static int width_2 = 1;
void MouseMarkEffect::reconfigure(ReconfigureFlags)
{
    m_freedraw_modifiers = Qt::KeyboardModifiers();
    m_arrowdraw_modifiers = Qt::KeyboardModifiers();
    MouseMarkConfig::self()->read();
    width = MouseMarkConfig::lineWidth();
    width_2 = width / 2;
    color = MouseMarkConfig::color();
    color.setAlphaF(1.0);
    if (MouseMarkConfig::freedrawshift()) {
        m_freedraw_modifiers |= Qt::ShiftModifier;
    }
    if (MouseMarkConfig::freedrawalt()) {
        m_freedraw_modifiers |= Qt::AltModifier;
    }
    if (MouseMarkConfig::freedrawcontrol()) {
        m_freedraw_modifiers |= Qt::ControlModifier;
    }
    if (MouseMarkConfig::freedrawmeta()) {
        m_freedraw_modifiers |= Qt::MetaModifier;
    }

    if (MouseMarkConfig::arrowdrawshift()) {
        m_arrowdraw_modifiers |= Qt::ShiftModifier;
    }
    if (MouseMarkConfig::arrowdrawalt()) {
        m_arrowdraw_modifiers |= Qt::AltModifier;
    }
    if (MouseMarkConfig::arrowdrawcontrol()) {
        m_arrowdraw_modifiers |= Qt::ControlModifier;
    }
    if (MouseMarkConfig::arrowdrawmeta()) {
        m_arrowdraw_modifiers |= Qt::MetaModifier;
    }
}

void MouseMarkEffect::paintScreen(effect::screen_paint_data& data)
{
    effects->paintScreen(data);

    if (marks.isEmpty() && drawing.isEmpty())
        return;
    if (effects->isOpenGLCompositing()) {
        if (!GLPlatform::instance()->isGLES()) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            glEnable(GL_LINE_SMOOTH);
            glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
        }
        glLineWidth(width);
        GLVertexBuffer* vbo = GLVertexBuffer::streamingBuffer();
        vbo->reset();
        ShaderBinder binder(ShaderTrait::UniformColor);
        binder.shader()->setUniform(GLShader::ModelViewProjectionMatrix, effect::get_mvp(data));
        binder.shader()->setUniform(GLShader::ColorUniform::Color, color);
        QVector<QVector2D> verts;
        for (auto const& mark : qAsConst(marks)) {
            verts.clear();
            verts.reserve(mark.size() * 2);
            for (auto const& p : qAsConst(mark)) {
                verts.push_back(QVector2D(p.x(), p.y()));
            }
            vbo->setVertices(verts);
            vbo->render(GL_LINE_STRIP);
        }
        if (!drawing.isEmpty()) {
            verts.clear();
            verts.reserve(drawing.size() * 2);
            for (auto const& p : qAsConst(drawing)) {
                verts.push_back(QVector2D(p.x(), p.y()));
            }
            vbo->setVertices(verts);
            vbo->render(GL_LINE_STRIP);
        }
        glLineWidth(1.0);
        if (!GLPlatform::instance()->isGLES()) {
            glDisable(GL_LINE_SMOOTH);
            glDisable(GL_BLEND);
        }
    } else {
        // Assume QPainter compositing.
        QPainter* painter = effects->scenePainter();
        painter->save();
        QPen pen(color);
        pen.setWidth(width);
        painter->setPen(pen);
        for (auto const& mark : qAsConst(marks)) {
            drawMark(painter, mark);
        }
        drawMark(painter, drawing);
        painter->restore();
    }
}

void MouseMarkEffect::drawMark(QPainter* painter, const Mark& mark)
{
    if (mark.count() <= 1) {
        return;
    }
    for (int i = 0; i < mark.count() - 1; ++i) {
        painter->drawLine(mark[i], mark[i + 1]);
    }
}

void MouseMarkEffect::slotMouseChanged(const QPoint& pos,
                                       const QPoint&,
                                       Qt::MouseButtons,
                                       Qt::MouseButtons,
                                       Qt::KeyboardModifiers modifiers,
                                       Qt::KeyboardModifiers)
{
    qCDebug(KWIN_MOUSEMARK) << "MouseChanged" << pos;
    if (modifiers == m_arrowdraw_modifiers
        && m_arrowdraw_modifiers != Qt::NoModifier) { // start/finish arrow
        if (arrow_tail != nullPoint()) {
            if (drawing.length() != 0) {
                clearLast(); // clear our arrow with tail at previous position
            }
            drawing = createArrow(pos, arrow_tail);
            effects->addRepaintFull();
            return;
        } else {
            if (drawing.length() > 0) { // has unfinished freedraw right before arrowdraw
                marks.append(drawing);
                drawing.clear();
            }
            arrow_tail = pos;
        }
    } else if (modifiers == m_freedraw_modifiers && m_freedraw_modifiers != Qt::NoModifier) {
        // activated
        if (arrow_tail != nullPoint()) {
            // for the case when user started freedraw right after arrowdraw
            arrow_tail = nullPoint();
            marks.append(drawing);
            drawing.clear();
        }
        if (drawing.isEmpty()) {
            drawing.append(pos);
        }
        if (drawing.last() == pos)
            return;
        QPoint pos2 = drawing.last();
        drawing.append(pos);
        QRect repaint = QRect(qMin(pos.x(), pos2.x()),
                              qMin(pos.y(), pos2.y()),
                              qMax(pos.x(), pos2.x()),
                              qMax(pos.y(), pos2.y()));
        repaint.adjust(-width, -width, width, width);
        effects->addRepaint(repaint);
    } else { // neither freedraw, nor arrowdraw modifiers pressed, but mouse moved
        if (drawing.length() > 1) {
            marks.append(drawing);
            drawing.clear();
        }
        arrow_tail = nullPoint();
    }
}

void MouseMarkEffect::clear()
{
    arrow_tail = nullPoint();
    drawing.clear();
    marks.clear();
    effects->addRepaintFull();
}

void MouseMarkEffect::clearLast()
{
    if (drawing.length() > 1) { // just pressing a modifiers already create a drawing with 1 point
                                // (so not visible), treat it as non-existent
        drawing.clear();
        effects->addRepaintFull();
    } else if (!marks.isEmpty()) {
        marks.pop_back();
        effects->addRepaintFull();
    }
}

MouseMarkEffect::Mark MouseMarkEffect::createArrow(QPoint arrow_head, QPoint arrow_tail)
{
    Mark ret;
    double angle = atan2((double)(arrow_tail.y() - arrow_head.y()),
                         (double)(arrow_tail.x() - arrow_head.x()));
    // Arrow is made of connected lines. We make it's last point at tail, so freedraw can begin from
    // the tail
    ret += arrow_head;
    ret += arrow_head + QPoint(50 * cos(angle + M_PI / 6),
                               50 * sin(angle + M_PI / 6)); // right one
    ret += arrow_head;
    ret += arrow_head + QPoint(50 * cos(angle - M_PI / 6),
                               50 * sin(angle - M_PI / 6)); // left one
    ret += arrow_head;
    ret += arrow_tail;
    return ret;
}

void MouseMarkEffect::screenLockingChanged(bool locked)
{
    if (!marks.isEmpty() || !drawing.isEmpty()) {
        effects->addRepaintFull();
    }
    // disable mouse polling while screen is locked.
    if (locked) {
        effects->stopMousePolling();
    } else {
        effects->startMousePolling();
    }
}

bool MouseMarkEffect::isActive() const
{
    return (!marks.isEmpty() || !drawing.isEmpty()) && !effects->isScreenLocked();
}

int MouseMarkEffect::requestedEffectChainPosition() const
{
    return 10;
}

}
