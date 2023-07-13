/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwineffects/effect.h>
#include <kwineffects/effect_window.h>
#include <kwineffects/window_quad.h>

#include <QMatrix4x4>

namespace KWin
{

class EffectScreen;
class GLShader;

namespace effect
{

struct paint_data {
    int mask;
    QRegion region;

    struct {
        QVector3D scale{1., 1., 1.};
        QVector3D translation;

        struct {
            QVector3D axis;
            QVector3D origin;
            qreal angle;
        } rotation;
    } geo;

    double opacity{1.};
    double saturation{1.};
    double brightness{1.};

    QMatrix4x4 projection_matrix;
    QMatrix4x4 model_view_matrix;
    QMatrix4x4 screen_projection_matrix;
};

struct screen_paint_data {
    EffectScreen const* screen;
    paint_data paint;
};

struct window_prepaint_data {
    EffectWindow& window;
    paint_data paint;

    /// Subtracted from paint region of following windows (window covers its clip region).
    QRegion clip;
    WindowQuadList quads;

    void set_translucent()
    {
        paint.mask |= Effect::PAINT_WINDOW_TRANSLUCENT;
        paint.mask &= ~Effect::PAINT_WINDOW_OPAQUE;

        // cannot clip, will be transparent
        clip = QRegion();
    }
};

struct window_paint_data {
    window_paint_data(EffectWindow& window, paint_data paint)
        : window{window}
        , paint{std::move(paint)}
    {
        quads = window.buildQuads();
        paint.opacity = window.opacity();
    }

    EffectWindow& window;
    paint_data paint;
    WindowQuadList quads;

    double cross_fade_progress{1.};
    GLShader* shader{nullptr};
};

}
}
