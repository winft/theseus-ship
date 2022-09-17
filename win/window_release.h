/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "geo.h"
#include "meta.h"
#include "remnant.h"
#include "space_window_release.h"
#include "x11/win_info.h"

namespace KWin::win
{

template<typename Win>
win::remnant create_remnant(Win& source)
{
    win::remnant remnant;

    remnant.data.frame_margins = win::frame_margins(&source);
    remnant.data.render_region = source.render_region();
    remnant.data.buffer_scale = source.bufferScale();
    remnant.data.desk = source.desktop();
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

    if (source.transient()->annexed) {
        remnant.refcount += source.transient()->leads().size();
    }

    remnant.data.was_group_transient = source.groupTransient();

    remnant.data.was_wayland_client = source.is_wayland_window();
    remnant.data.was_x11_client = source.isClient();
    remnant.data.was_popup_window = win::is_popup(&source);
    remnant.data.was_lock_screen = source.isLockScreen();

    return remnant;
}

template<typename Win>
void transfer_remnant_data(Win& source, Win& dest)
{
    dest.internal_id = source.internal_id;
    dest.m_frameGeometry = source.m_frameGeometry;
    dest.xcb_visual = source.xcb_visual;
    dest.bit_depth = source.bit_depth;

    dest.window_type = source.windowType();
    dest.info = source.info;
    if (auto winfo = dynamic_cast<x11::win_info<Win>*>(dest.info)) {
        winfo->disable();
    }

    dest.xcb_window.reset(source.xcb_window, false);
    dest.ready_for_painting = source.ready_for_painting;
    dest.damage_handle = XCB_NONE;
    dest.damage_region = source.damage_region;
    dest.repaints_region = source.repaints_region;
    dest.layer_repaints_region = source.layer_repaints_region;
    dest.is_shape = source.is_shape;
    dest.is_outline = source.is_outline;

    dest.render = std::move(source.render);

    dest.resource_name = source.resource_name;
    dest.resource_class = source.resource_class;

    dest.opaque_region = source.opaque_region;
    dest.central_output = source.central_output;
    dest.m_skipCloseAnimation = source.m_skipCloseAnimation;
    dest.m_desktops = source.desktops();
    dest.m_layer = source.layer();
    dest.has_in_content_deco = source.has_in_content_deco;
    dest.client_frame_extents = source.client_frame_extents;

    dest.transient()->annexed = source.transient()->annexed;
    dest.transient()->set_modal(source.transient()->modal());

    auto const leads = source.transient()->leads();
    for (auto const& lead : leads) {
        lead->transient()->add_child(&dest);
        lead->transient()->remove_child(&source);
    }

    auto const children = source.transient()->children;
    for (auto const& child : children) {
        dest.transient()->add_child(child);
        source.transient()->remove_child(child);
    }

    auto const desktops = dest.desktops();
    for (auto vd : desktops) {
        QObject::connect(vd, &QObject::destroyed, dest.qobject.get(), [vd, dest_ptr = &dest] {
            auto desks = dest_ptr->desktops();
            desks.removeOne(vd);
            dest_ptr->set_desktops(desks);
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
    if (!source.ready_for_painting) {
        // Don't create remnants for windows that have never been shown.
        return nullptr;
    }

    return new Win(create_remnant(source), source.space);
}

}
