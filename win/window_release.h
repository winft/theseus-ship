/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "remnant.h"
#include "space_helpers.h"
#include "toplevel.h"

namespace KWin::win
{

template<typename RemnantWin, typename Win>
RemnantWin* create_remnant(Win& source)
{
    if (!source.space.render.scene) {
        // Don't create effect remnants when we don't render.
        return nullptr;
    }
    if (!source.ready_for_painting) {
        // Don't create remnants for windows that have never been shown.
        return nullptr;
    }

    auto win = new RemnantWin(source.space);
    win->copyToDeleted(&source);

    auto remnant = std::make_unique<win::remnant<Toplevel>>(*win);

    remnant->frame_margins = win::frame_margins(&source);
    remnant->render_region = source.render_region();
    remnant->buffer_scale = source.bufferScale();
    remnant->desk = source.desktop();
    remnant->frame = source.frameId();
    remnant->opacity = source.opacity();
    remnant->window_type = source.windowType();
    remnant->window_role = source.windowRole();

    if (source.control) {
        remnant->no_border = source.noBorder();
        if (!remnant->no_border) {
            source.layoutDecorationRects(remnant->decoration_left,
                                         remnant->decoration_top,
                                         remnant->decoration_right,
                                         remnant->decoration_bottom);
            if (win::decoration(&source)) {
                remnant->decoration_renderer = source.control->deco().client->move_renderer();
            }
        }
        remnant->minimized = source.control->minimized();

        remnant->fullscreen = source.control->fullscreen();
        remnant->keep_above = source.control->keep_above();
        remnant->keep_below = source.control->keep_below();
        remnant->caption = win::caption(&source);

        remnant->was_active = source.control->active();
    }

    win->transient()->annexed = source.transient()->annexed;

    auto const leads = source.transient()->leads();
    for (auto const& lead : leads) {
        lead->transient()->add_child(win);
        lead->transient()->remove_child(&source);
        if (win->transient()->annexed) {
            remnant->refcount++;
        }
    }

    auto const children = source.transient()->children;
    for (auto const& child : children) {
        win->transient()->add_child(child);
        source.transient()->remove_child(child);
    }

    win->transient()->set_modal(source.transient()->modal());
    remnant->was_group_transient = source.groupTransient();

    auto const desktops = win->desktops();
    for (auto vd : desktops) {
        QObject::connect(vd, &QObject::destroyed, win, [=] {
            auto desks = win->desktops();
            desks.removeOne(vd);
            win->set_desktops(desks);
        });
    }

    remnant->was_wayland_client = source.is_wayland_window();
    remnant->was_x11_client = qobject_cast<win::x11::window*>(&source) != nullptr;
    remnant->was_popup_window = win::is_popup(&source);
    remnant->was_outline = source.isOutline();
    remnant->was_lock_screen = source.isLockScreen();

    if (source.control) {
        remnant->control = std::make_unique<win::control>(win);
    }

    win->remnant = std::move(remnant);

    win::add_remnant(source, *win);
    Q_EMIT source.remnant_created(win);
    return win;
}

}
