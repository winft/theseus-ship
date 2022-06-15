/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "deco.h"

#include "main.h"
#include "render/compositor.h"
#include "render/effects.h"
#include "render/shadow.h"
#include "render/window.h"

#include <cassert>

namespace KWin::win
{

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
QRect render_geometry(Win* win);

template<typename Win>
QRect visible_rect(Win* win, QRect const& frame_geo)
{
    auto geo = frame_geo + win->client_frame_extents;

    if (shadow(win) && !shadow(win)->shadowRegion().isEmpty()) {
        geo += shadow(win)->margins();
    }

    return geo;
}

template<typename Win>
QRect visible_rect(Win* win)
{
    return visible_rect(win, win->frameGeometry());
}

template<typename Win>
QRegion content_render_region(Win* win)
{
    auto const shape = win->render_region();
    auto clipping = QRect(QPoint(0, 0), render_geometry(win).size());

    if (win->has_in_content_deco) {
        clipping |= QRect(QPoint(0, 0), win->size());
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
        Q_EMIT win->shadowChanged();
    } else if (win->render) {
        win->render->create_shadow();
    }

    if (auto shdw = shadow(win)) {
        dirty_rect |= shdw->shadowRegion().boundingRect();
    }

    if (old_visible_rect != visible_rect(win)) {
        Q_EMIT win->paddingChanged(win, old_visible_rect);
    }

    if (dirty_rect.isValid()) {
        dirty_rect.translate(win->pos());
        win->addLayerRepaint(dirty_rect);
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
    win.render->effect = std::make_unique<render::effects_window_impl>(&win);

    QObject::connect(&win, &Win::remnant_created, &scene, [scene_ptr = &scene](auto remnant) {
        scene_ptr->init_remnant(*remnant);
    });
    QObject::connect(&win, &Win::central_output_changed, &scene, [&](auto old_out, auto new_out) {
        if (!new_out) {
            return;
        }
        if (old_out && old_out->scale() == new_out->scale()) {
            return;
        }
        scene.windowGeometryShapeChanged(&win);
    });

    auto scn_win = win.render.get();
    win.render->effect->setSceneWindow(scn_win);
    win.add_scene_window_addon();

    win::update_shadow(&win);
    QObject::connect(
        &win, &Win::shadowChanged, &scene, [scn_win] { scn_win->invalidateQuadsCache(); });
}

template<typename Win>
bool setup_compositing(Win& win, bool add_full_damage)
{
    static_assert(!Win::is_toplevel);
    assert(!win.remnant);

    if (!win.space.compositing()) {
        return false;
    }

    if (win.damage_handle != XCB_NONE) {
        return false;
    }

    if (kwinApp()->operationMode() == Application::OperationModeX11) {
        assert(!win.surface);
        win.damage_handle = xcb_generate_id(connection());
        xcb_damage_create(
            connection(), win.damage_handle, win.frameId(), XCB_DAMAGE_REPORT_LEVEL_NON_EMPTY);
    }

    win.discard_shape();
    win.damage_region = QRegion(QRect(QPoint(), win.size()));

    add_scene_window(*win.space.render.scene, win);

    if (add_full_damage) {
        // With unmanaged windows there is a race condition between the client painting the window
        // and us setting up damage tracking.  If the client wins we won't get a damage event even
        // though the window has been painted.  To avoid this we mark the whole window as damaged
        // and schedule a repaint immediately after creating the damage object.
        // TODO: move this out of the class.
        win.addDamageFull();
    }

    return true;
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
    win->space.render.addRepaint(visible_rect(win));
}

}
