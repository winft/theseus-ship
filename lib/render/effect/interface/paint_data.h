/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <render/effect/interface/effect.h>
#include <render/effect/interface/effect_window.h>
#include <render/effect/interface/window_quad.h>
#include <render/interface/framebuffer.h>

#include <QMatrix4x4>
#include <chrono>
#include <stack>

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
};

struct render_data {
    std::stack<render::framebuffer*>& targets;

    QMatrix4x4 const view;
    QMatrix4x4 const projection;

    QRect const viewport;
    transform_type const transform{transform_type::normal};

    // If the render backend requires an additional flip of the y-axis.
    bool const flip_y{false};
};

struct screen_prepaint_data {
    EffectScreen const& screen;
    paint_data paint;
    render_data render;
    std::chrono::milliseconds const present_time;
};

struct screen_paint_data {
    EffectScreen const* screen;
    paint_data paint;
    render_data render;
};

struct window_prepaint_data {
    EffectWindow& window;
    paint_data paint;

    /// Subtracted from paint region of following windows (window covers its clip region).
    QRegion clip;
    WindowQuadList quads;
    std::chrono::milliseconds const present_time;

    void set_translucent()
    {
        paint.mask |= Effect::PAINT_WINDOW_TRANSLUCENT;
        paint.mask &= ~Effect::PAINT_WINDOW_OPAQUE;

        // cannot clip, will be transparent
        clip = QRegion();
    }
};

struct window_paint_data {
    window_paint_data(EffectWindow& window, paint_data paint, render_data render)
        : window_paint_data(window, std::move(paint), window.buildQuads(), std::move(render))
    {
    }

    window_paint_data(EffectWindow& window,
                      paint_data paint,
                      WindowQuadList const& quads,
                      render_data render)
        : window{window}
        , paint{std::move(paint)}
        , quads{quads}
        , render{std::move(render)}
    {
        paint.opacity = window.opacity();
    }

    EffectWindow& window;

    // Applied after paint geometry.
    QMatrix4x4 model;

    paint_data paint;
    WindowQuadList quads;
    render_data render;

    double cross_fade_progress{1.};
    GLShader* shader{nullptr};
};

template<typename Data>
QMatrix4x4 get_mvp(Data const& data)
{
    QMatrix4x4 geo;
    geo.translate(data.paint.geo.translation);
    geo.scale(data.paint.geo.scale.x(), data.paint.geo.scale.y(), data.paint.geo.scale.z());

    if (auto const& rot = data.paint.geo.rotation; !qFuzzyCompare(rot.angle, 0.0)) {
        geo.translate(rot.origin);
        geo.rotate(rot.angle, rot.axis.x(), rot.axis.y(), rot.axis.z());
        geo.translate(-rot.origin);
    }

    if constexpr (requires(Data data) { data.model; }) {
        return data.render.projection * data.render.view * data.model * geo;
    }
    return data.render.projection * data.render.view * geo;
}

inline QMatrix4x4 get_viewport_matrix(render_data const& render)
{
    auto const& vp = render.viewport;
    QMatrix4x4 vp_matrix;

    vp_matrix.translate(vp.x(), vp.y());
    vp_matrix.translate(vp.width() / 2., vp.height() / 2.);
    vp_matrix.scale(vp.width() / 2., vp.height() / 2.);

    if (render.flip_y) {
        vp_matrix.scale(1, -1);
    }

    return vp_matrix;
}

inline QRect map_to_viewport(render_data const& render, QRect const& rect)
{
    return (get_viewport_matrix(render) * render.projection * render.view).mapRect(rect);
}

inline QRegion map_to_viewport(render_data const& render, QRegion const& region)
{
    QRegion ret;
    for (auto const& rect : region) {
        ret |= map_to_viewport(render, rect);
    }
    return ret;
}

}
}
