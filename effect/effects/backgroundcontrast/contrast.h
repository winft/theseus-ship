/*
 *   Copyright © 2010 Fredrik Höglund <fredrik@kde.org>
 *   Copyright 2014 Marco Martin <mart@kde.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; see the file COPYING.  if not, write to
 *   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *   Boston, MA 02110-1301, USA.
 */
#ifndef CONTRAST_H
#define CONTRAST_H

#include <kwineffects/effect.h>
#include <kwingl/platform.h>
#include <kwingl/utils.h>

#include <QVector2D>
#include <QVector>
#include <memory>

namespace KWin
{

class ContrastShader;

class ContrastEffect : public KWin::Effect
{
    Q_OBJECT
public:
    ContrastEffect();
    ~ContrastEffect() override;

    static bool supported();
    static bool enabledByDefault();

    void reconfigure(ReconfigureFlags flags) override;
    void
    drawWindow(EffectWindow* w, int mask, const QRegion& region, WindowPaintData& data) override;
    void paintEffectFrame(EffectFrame* frame,
                          const QRegion& region,
                          double opacity,
                          double frameOpacity) override;

    bool provides(Feature feature) override;
    bool isActive() const override;

    int requestedEffectChainPosition() const override
    {
        return 76;
    }

    void slotWindowDeleted(KWin::EffectWindow* w);
    void reset();

    QHash<const EffectWindow*, QMatrix4x4> m_colorMatrices;

private:
    QRegion contrastRegion(const EffectWindow* w) const;
    bool shouldContrast(const EffectWindow* w, int mask, const WindowPaintData& data) const;
    void doContrast(EffectWindow* w,
                    const QRegion& shape,
                    const QRect& screen,
                    const float opacity,
                    const QMatrix4x4& screenProjection);
    void uploadRegion(QVector2D*& map, const QRegion& region);
    void uploadGeometry(GLVertexBuffer* vbo, const QRegion& region);

private:
    ContrastShader* shader;
};

inline bool ContrastEffect::provides(Effect::Feature feature)
{
    if (feature == Contrast) {
        return true;
    }
    return KWin::Effect::provides(feature);
}

} // namespace KWin

#endif
