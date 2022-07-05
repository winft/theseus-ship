/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "geo.h"
#include "remnant.h"
#include "space_window_release.h"
#include "x11/netinfo.h"

#include "toplevel.h"

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
    remnant.data.window_type = source.windowType();
    remnant.data.window_role = source.windowRole();

    if (source.control) {
        remnant.data.no_border = source.noBorder();
        if (!remnant.data.no_border) {
            source.layoutDecorationRects(remnant.data.decoration_left,
                                         remnant.data.decoration_top,
                                         remnant.data.decoration_right,
                                         remnant.data.decoration_bottom);
            if (win::decoration(&source)) {
                remnant.data.decoration_renderer = source.control->deco().client->move_renderer();
            }
        }
        remnant.data.minimized = source.control->minimized();

        remnant.data.fullscreen = source.control->fullscreen();
        remnant.data.keep_above = source.control->keep_above();
        remnant.data.keep_below = source.control->keep_below();
        remnant.data.caption = win::caption(&source);

        remnant.data.was_active = source.control->active();
    }

    if (source.transient()->annexed) {
        remnant.refcount += source.transient()->leads().size();
    }

    remnant.data.was_group_transient = source.groupTransient();

    remnant.data.was_wayland_client = source.is_wayland_window();
    remnant.data.was_x11_client = qobject_cast<win::x11::window*>(&source) != nullptr;
    remnant.data.was_popup_window = win::is_popup(&source);
    remnant.data.was_outline = source.isOutline();
    remnant.data.was_lock_screen = source.isLockScreen();

    return remnant;
}

template<typename RemnantWin, typename Win>
RemnantWin* create_remnant_window(Win& source)
{
    if (!source.space.render.scene) {
        // Don't create effect remnants when we don't render.
        return nullptr;
    }
    if (!source.ready_for_painting) {
        // Don't create remnants for windows that have never been shown.
        return nullptr;
    }

    auto win = new RemnantWin(create_remnant(source), source.space);

    win->internal_id = source.internal_id;
    win->m_frameGeometry = source.m_frameGeometry;
    win->xcb_visual = source.xcb_visual;
    win->bit_depth = source.bit_depth;

    win->info = source.info;
    if (auto winfo = dynamic_cast<win::x11::win_info*>(win->info)) {
        winfo->disable();
    }

    win->xcb_window.reset(source.xcb_window, false);
    win->ready_for_painting = source.ready_for_painting;
    win->damage_handle = XCB_NONE;
    win->damage_region = source.damage_region;
    win->repaints_region = source.repaints_region;
    win->layer_repaints_region = source.layer_repaints_region;
    win->is_shape = source.is_shape;

    win->render = std::move(source.render);
    if (win->render) {
        win->render->effect->setWindow(win);
    }

    win->resource_name = source.resource_name;
    win->resource_class = source.resource_class;

    win->client_machine = source.client_machine;
    win->m_wmClientLeader = source.wmClientLeader();

    win->opaque_region = source.opaque_region;
    win->central_output = source.central_output;
    win->m_skipCloseAnimation = source.m_skipCloseAnimation;
    win->m_desktops = source.desktops();
    win->m_layer = source.layer();
    win->has_in_content_deco = source.has_in_content_deco;
    win->client_frame_extents = source.client_frame_extents;

    win->transient()->annexed = source.transient()->annexed;
    win->transient()->set_modal(source.transient()->modal());

    auto const leads = source.transient()->leads();
    for (auto const& lead : leads) {
        lead->transient()->add_child(win);
        lead->transient()->remove_child(&source);
    }

    auto const children = source.transient()->children;
    for (auto const& child : children) {
        win->transient()->add_child(child);
        source.transient()->remove_child(child);
    }

    auto const desktops = win->desktops();
    for (auto vd : desktops) {
        QObject::connect(vd, &QObject::destroyed, win, [=] {
            auto desks = win->desktops();
            desks.removeOne(vd);
            win->set_desktops(desks);
        });
    }

    win::add_remnant(source, *win);
    Q_EMIT source.remnant_created(win);
    return win;
}

}
