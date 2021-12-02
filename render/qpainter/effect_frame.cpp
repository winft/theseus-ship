/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "effect_frame.h"

#include "scene.h"

#include "render/effects.h"

#include <QPainter>

namespace KWin::render::qpainter
{

effect_frame::effect_frame(effect_frame_impl* frame, qpainter::scene* scene)
    : render::effect_frame(frame)
    , m_scene(scene)
{
}

effect_frame::~effect_frame() = default;

void effect_frame::render(QRegion region, double opacity, double frameOpacity)
{
    Q_UNUSED(region)
    Q_UNUSED(opacity)
    // TODO: adjust opacity
    if (m_effectFrame->geometry().isEmpty()) {
        return; // Nothing to display
    }
    QPainter* painter = m_scene->scenePainter();

    // Render the actual frame
    if (m_effectFrame->style() == EffectFrameUnstyled) {
        painter->save();
        painter->setPen(Qt::NoPen);
        QColor color(Qt::black);
        color.setAlphaF(frameOpacity);
        painter->setBrush(color);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->drawRoundedRect(m_effectFrame->geometry().adjusted(-5, -5, 5, 5), 5.0, 5.0);
        painter->restore();
    } else if (m_effectFrame->style() == EffectFrameStyled) {
        qreal left, top, right, bottom;
        m_effectFrame->frame().getMargins(
            left, top, right, bottom); // m_geometry is the inner geometry
        QRect geom = m_effectFrame->geometry().adjusted(-left, -top, right, bottom);
        painter->drawPixmap(geom, m_effectFrame->frame().framePixmap());
    }
    if (!m_effectFrame->selection().isNull()) {
        painter->drawPixmap(m_effectFrame->selection(),
                            m_effectFrame->selectionFrame().framePixmap());
    }

    // Render icon
    if (!m_effectFrame->icon().isNull() && !m_effectFrame->iconSize().isEmpty()) {
        const QPoint topLeft(m_effectFrame->geometry().x(),
                             m_effectFrame->geometry().center().y()
                                 - m_effectFrame->iconSize().height() / 2);

        const QRect geom = QRect(topLeft, m_effectFrame->iconSize());
        painter->drawPixmap(geom, m_effectFrame->icon().pixmap(m_effectFrame->iconSize()));
    }

    // Render text
    if (!m_effectFrame->text().isEmpty()) {
        // Determine position on texture to paint text
        QRect rect(QPoint(0, 0), m_effectFrame->geometry().size());
        if (!m_effectFrame->icon().isNull() && !m_effectFrame->iconSize().isEmpty()) {
            rect.setLeft(m_effectFrame->iconSize().width());
        }

        // If static size elide text as required
        QString text = m_effectFrame->text();
        if (m_effectFrame->isStatic()) {
            QFontMetrics metrics(m_effectFrame->text());
            text = metrics.elidedText(text, Qt::ElideRight, rect.width());
        }

        painter->save();
        painter->setFont(m_effectFrame->font());
        if (m_effectFrame->style() == EffectFrameStyled) {
            painter->setPen(m_effectFrame->styledTextColor());
        } else {
            // TODO: What about no frame? Custom color setting required
            painter->setPen(Qt::white);
        }
        painter->drawText(
            rect.translated(m_effectFrame->geometry().topLeft()), m_effectFrame->alignment(), text);
        painter->restore();
    }
}

}
