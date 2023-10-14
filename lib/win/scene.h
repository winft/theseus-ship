/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "deco.h"
#include "window_qobject.h"
#include <utils/algorithm.h>
#include <win/geo.h>

#include <cassert>

namespace KWin::win
{

template<typename Win>
bool has_alpha(Win& win)
{
    return win.render_data.bit_depth == 32;
}

template<typename Win>
bool wants_shadow_to_be_rendered(Win const& win)
{
    return win.control && !win.control->fullscreen && win.maximizeMode() != maximize_mode::full;
}

template<typename Win>
void set_bit_depth(Win& win, int depth)
{
    if (win.render_data.bit_depth == depth) {
        return;
    }

    auto const old_alpha = has_alpha(win);
    win.render_data.bit_depth = depth;

    if (old_alpha != has_alpha(win)) {
        Q_EMIT win.qobject->hasAlphaChanged();
    }
}

/**
 * Returns the pointer to the window's shadow. A shadow is only available if Compositing is enabled
 * and on X11 if the corresponding X window has the shadow property set.
 *
 * @returns The shadow belonging to @param win, @c null if there's no shadow.
 */
template<typename Win>
auto shadow(Win* win)
{
    return win->render ? win->render->shadow() : nullptr;
}

template<typename Win>
void acquire_repaint_outputs(Win& win, QRegion const& region)
{
    for (auto& out : win.space.base.outputs) {
        if (contains(win.render_data.repaint_outputs, out)) {
            continue;
        }
        if (region.intersected(out->geometry()).isEmpty()) {
            continue;
        }
        win.render_data.repaint_outputs.push_back(out);
    }
}

template<typename Win>
void add_layer_repaint(Win& win, QRegion const& region)
{
    if (!win.space.base.render->scene) {
        return;
    }
    win.render_data.layer_repaints_region += region;
    acquire_repaint_outputs(win, region);
    Q_EMIT win.qobject->needsRepaint();
}

template<typename Win>
QRect render_geometry(Win* win);

template<typename Win>
QRect visible_rect(Win* win, QRect const& frame_geo)
{
    auto geo = frame_geo + win->geo.client_frame_extents;

    if (shadow(win) && !shadow(win)->shadowRegion().isEmpty()) {
        geo += shadow(win)->margins();
    }

    return geo;
}

template<typename Win>
QRect visible_rect(Win* win)
{
    return visible_rect(win, win->geo.frame);
}

template<typename Win>
void add_full_repaint(Win& win)
{
    auto const region = visible_rect(&win);
    win.render_data.repaints_region = region.translated(-win.geo.pos());

    for (auto child : win.transient->children) {
        if (child->transient->annexed) {
            add_full_repaint(*child);
        }
    }

    acquire_repaint_outputs(win, region);
    Q_EMIT win.qobject->needsRepaint();
}

template<typename Win>
void discard_shape(Win& win)
{
    if constexpr (requires(Win win) { win.is_render_shape_valid; }) {
        win.is_render_shape_valid = false;
    }

    if (win.render) {
        win.render->invalidateQuadsCache();
        add_full_repaint(win);
    }
    if (win.transient->annexed) {
        for (auto lead : win.transient->leads()) {
            discard_shape(*lead);
        }
    }
}

template<typename Win>
QRegion content_render_region(Win* win)
{
    auto const shape = win->render_region();
    auto clipping = QRect(QPoint(0, 0), render_geometry(win).size());

    if (win->geo.has_in_content_deco) {
        clipping |= QRect(QPoint(0, 0), win->geo.size());
        auto const tl_offset = QPoint(left_border(win), top_border(win));
        auto const br_offset = -QPoint(right_border(win), bottom_border(win));

        clipping = QRect(tl_offset, clipping.bottomRight() + br_offset);
    }

    return shape & clipping;
}

/**
 * Updates the shadow associated with @param win.
 * Call this method when the windowing system notifies a change or compositing is started.
 */
template<typename Win>
auto update_shadow(Win* win)
{
    // Old & new shadow region
    QRect dirty_rect;

    auto const old_visible_rect = visible_rect(win);

    if (auto shdw = shadow(win)) {
        dirty_rect = shdw->shadowRegion().boundingRect();
        if (!shdw->updateShadow()) {
            win->render->updateShadow(nullptr);
        }
        Q_EMIT win->qobject->shadowChanged();
    } else if (win->render) {
        win->render->create_shadow();
    }

    if (auto shdw = shadow(win)) {
        dirty_rect |= shdw->shadowRegion().boundingRect();
    }

    if (old_visible_rect != visible_rect(win)) {
        Q_EMIT win->qobject->paddingChanged(old_visible_rect);
    }

    if (dirty_rect.isValid()) {
        dirty_rect.translate(win->geo.pos());
        add_layer_repaint(*win, dirty_rect);
    }
}

/**
 * Adds the window to the scene.
 *
 * If the window gets deleted, then the scene will try automatically
 * to re-bind an underlying scene window to the corresponding remnant.
 *
 * @param win The window to be added.
 * @note You can add a toplevel to scene only once.
 */
template<typename Scene, typename Win>
void add_scene_window(Scene& scene, Win& win)
{
    assert(!win.render);

    win.render = scene.createWindow(&win);
    win.render->effect
        = std::make_unique<typename decltype(win.render->effect)::element_type>(*win.render);

    QObject::connect(win.qobject.get(),
                     &window_qobject::central_output_changed,
                     &scene,
                     [&](auto old_out, auto new_out) {
                         if (!new_out) {
                             return;
                         }
                         if (old_out && old_out->scale() == new_out->scale()) {
                             return;
                         }
                         scene.windowGeometryShapeChanged(&win);
                     });

    auto scn_win = win.render.get();
    win.add_scene_window_addon();

    win::update_shadow(&win);
    QObject::connect(win.qobject.get(), &window_qobject::shadowChanged, &scene, [scn_win] {
        scn_win->invalidateQuadsCache();
    });
}

/**
 * Window will be temporarily painted as if being at the top of the stack.
 * Only available if Compositor is active, if not active, this method is a no-op.
 */
template<typename Win>
void elevate(Win* win, bool elevate)
{
    if (!win->render) {
        return;
    }

    win->render->effect->elevate(elevate);
    win->space.base.render->addRepaint(visible_rect(win));
}

template<typename Win>
void add_full_damage(Win& win)
{
    if (!win.space.base.render->scene) {
        return;
    }

    auto const render_geo = frame_to_render_rect(&win, win.geo.frame);

    auto const damage = QRect({}, render_geo.size());
    win.render_data.damage_region = damage;

    auto repaint = damage;
    if (win.geo.has_in_content_deco) {
        repaint.translate(-QPoint(left_border(&win), top_border(&win)));
    }
    win.render_data.repaints_region |= repaint;
    acquire_repaint_outputs(win, render_geo);

    Q_EMIT win.qobject->damaged(win.render_data.damage_region);
}

template<typename Win>
void discard_buffer(Win& win)
{
    add_full_damage(win);
    if (win.render) {
        win.render->discard_buffer();
    }
}

template<typename Win>
void finish_compositing(Win& win)
{
    assert(!win.remnant);

    if (win.render) {
        discard_buffer(win);
        win.render.reset();
    }

    win.render_data.damage_region = {};
    win.render_data.repaints_region = {};
}

template<typename Win>
void scene_add_remnant(Win& win)
{
    assert(win.render);

    win.render->ref_win = &win;

    if (auto shadow = win.render->shadow()) {
        QObject::connect(win.qobject.get(),
                         &win::window_qobject::frame_geometry_changed,
                         shadow,
                         &std::remove_pointer_t<decltype(shadow)>::geometryChanged);
    }

    if (auto& effects = win.space.base.render->effects) {
        Q_EMIT effects->windowClosed(win.render->effect.get());
    }
}

template<typename Win>
bool is_blocking_compositing(Win& win)
{
    if constexpr (requires(Win win) { win.isBlockingCompositing(); }) {
        return win.isBlockingCompositing();
    }
    return false;
}

template<typename Win>
void set_blocking_compositing(Win& win, bool block)
{
    if constexpr (requires(Win win) { win.setBlockingCompositing(); }) {
        return win.setBlockingCompositing(block);
    }
}

}
