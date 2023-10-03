/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "geo.h"
#include "meta.h"
#include "remnant.h"
#include "space_window_release.h"
#include "transient.h"

namespace KWin::win
{

namespace x11
{
template<typename Win>
class win_info;
}

template<typename Win>
win::remnant create_remnant(Win& source)
{
    win::remnant remnant;

    remnant.data.frame_margins = win::frame_margins(&source);
    remnant.data.render_region = source.render_region();

    if constexpr (requires(Win win) { win.bufferScale(); }) {
        remnant.data.buffer_scale = source.bufferScale();
    }

    remnant.data.desk = get_subspace(source);
    remnant.data.frame = source.frameId();
    remnant.data.opacity = source.opacity();
    remnant.data.window_role = source.windowRole();

    if (source.control) {
        remnant.data.no_border = source.noBorder();
        if (!remnant.data.no_border) {
            source.layoutDecorationRects(remnant.data.decoration_left,
                                         remnant.data.decoration_top,
                                         remnant.data.decoration_right,
                                         remnant.data.decoration_bottom);
            if (win::decoration(&source)) {
                remnant.data.deco_render = source.control->deco.client->move_renderer();
            }
        }
        remnant.data.minimized = source.control->minimized;

        remnant.data.fullscreen = source.control->fullscreen;
        remnant.data.keep_above = source.control->keep_above;
        remnant.data.keep_below = source.control->keep_below;
        remnant.data.caption = win::caption(&source);

        remnant.data.was_active = source.control->active;
    }

    if (source.transient->annexed) {
        remnant.refcount += source.transient->leads().size();
    }

    remnant.data.was_group_transient = is_group_transient(source);

    if constexpr (requires(Win win) { win.is_wayland_window(); }) {
        remnant.data.was_wayland_client = source.is_wayland_window();
    }
    if constexpr (requires(Win win) { win.isClient(); }) {
        remnant.data.was_x11_client = source.isClient();
    }
    if constexpr (requires(Win win) { win.isLockScreen(); }) {
        remnant.data.was_lock_screen = source.isLockScreen();
    }

    remnant.data.was_popup_window = win::is_popup(&source);

    return remnant;
}

template<typename Win>
void transfer_remnant_data(Win& source, Win& dest)
{
    dest.meta.internal_id = source.meta.internal_id;
    dest.geo.frame = source.geo.frame;
    dest.render_data.bit_depth = source.render_data.bit_depth;

    if constexpr (requires(Win dest) { dest.window_type; }) {
        dest.window_type = source.windowType();
    }

    dest.render_data.ready_for_painting = source.render_data.ready_for_painting;
    dest.render_data.damage_region = source.render_data.damage_region;
    dest.render_data.repaints_region = source.render_data.repaints_region;
    dest.render_data.layer_repaints_region = source.render_data.layer_repaints_region;

    if constexpr (requires(Win win) { win.is_outline; }) {
        dest.is_outline = source.is_outline;
    }

    if constexpr (requires(Win win) { win.skip_close_animation; }) {
        dest.skip_close_animation = source.skip_close_animation;
    }

    dest.render = std::move(source.render);

    dest.meta.wm_class = source.meta.wm_class;
    dest.render_data.opaque_region = source.render_data.opaque_region;
    dest.topo.central_output = source.topo.central_output;
    dest.topo.subspaces = source.topo.subspaces;
    dest.topo.layer = get_layer(source);
    dest.geo.has_in_content_deco = source.geo.has_in_content_deco;
    dest.geo.client_frame_extents = source.geo.client_frame_extents;

    dest.transient->annexed = source.transient->annexed;
    dest.transient->set_modal(source.transient->modal());

    auto const leads = source.transient->leads();
    for (auto const& lead : leads) {
        lead->transient->add_child(&dest);
        lead->transient->remove_child(&source);
    }

    auto const children = source.transient->children;
    for (auto const& child : children) {
        dest.transient->add_child(child);
        source.transient->remove_child(child);
    }

    auto const subspaces = dest.topo.subspaces;
    for (auto sub : subspaces) {
        QObject::connect(sub, &QObject::destroyed, dest.qobject.get(), [sub, dest_ptr = &dest] {
            auto subs = dest_ptr->topo.subspaces;
            remove_all(subs, sub);
            dest_ptr->topo.subspaces = subs;
        });
    }
}

template<typename Win>
Win* create_remnant_window(Win& source)
{
    if (!source.space.base.render->compositor->scene) {
        // Don't create effect remnants when we don't render.
        return nullptr;
    }
    if (!source.render_data.ready_for_painting) {
        // Don't create remnants for windows that have never been shown.
        return nullptr;
    }

    return new Win(create_remnant(source), source.space);
}

}
