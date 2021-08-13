/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "window.h"

#include "render/compositor.h"
#include "win/space.h"
#include "win/transient.h"
#include "win/x11/stacking_tree.h"

#include <Wrapland/Server/subcompositor.h>
#include <Wrapland/Server/surface.h>

namespace KWin::win::wayland
{

template<typename Win>
void assign_subsurface_role(Win* win)
{
    assert(win->surface());
    assert(win->surface()->subsurface());

    win->transient()->annexed = true;
}

inline void restack_subsurfaces(Toplevel* window)
{
    auto subsurface_stacker = [&window](std::vector<Toplevel*>& children) {
        auto const& subsurfaces = window->surface()->childSubsurfaces();
        std::vector<Toplevel*> stacking;

        for (auto const& subsurface : subsurfaces) {
            auto surface = subsurface->surface();
            auto it = std::find_if(children.begin(), children.end(), [&surface](Toplevel* child) {
                return child->surface() == surface;
            });
            if (it == children.end()) {
                continue;
            }
            stacking.push_back(*it);
            children.erase(it);
        }

        children.insert(children.end(), stacking.begin(), stacking.end());
    };
    subsurface_stacker(window->transient()->children);

    // Optimize and do that only for the first window up the chain not being annexed.
    if (!window->transient()->annexed) {
        workspace()->x_stacking_tree->mark_as_dirty();
        workspace()->stacking_order->update(false);
    }
}

template<typename Win, typename Lead>
void set_subsurface_parent(Win* win, Lead* lead)
{
    namespace WS = Wrapland::Server;

    assert(!win->transient()->lead());
    assert(!contains(lead->transient()->children, win));

    lead->transient()->add_child(win);
    restack_subsurfaces(lead);

    QObject::connect(win->surface(), &WS::Surface::committed, win, &window::handle_commit);
    QObject::connect(win->surface(), &WS::Surface::sizeChanged, win, [win] {
        auto const old_geo = win->frameGeometry();
        // TODO(romangg): use setFrameGeometry?
        win->set_frame_geometry(QRect(win->pos(), win->surface()->size()));
        Q_EMIT win->frame_geometry_changed(win, old_geo);
    });

    QObject::connect(lead, &Lead::windowShown, win, [win] { win->map(); });
    QObject::connect(lead, &Lead::windowHidden, win, [win] { win->unmap(); });

    // TODO(romangg): Why is that needed again? weston-subsurfaces works without it, but Firefox
    //                stops rendering without this connection.
    QObject::connect(win, &Win::needsRepaint, render::compositor::self(), [win] {
        render::compositor::self()->schedule_repaint(win);
    });

    auto subsurface = win->surface()->subsurface();

    auto get_pos = [lead, subsurface] {
        return win::render_geometry(lead).topLeft() + subsurface->position();
    };
    auto set_pos = [win, get_pos]() {
        auto const old_frame_geo = win->frameGeometry();
        auto const frame_geo = QRect(get_pos(), win->surface()->size());

        if (old_frame_geo != frame_geo) {
            // TODO(romangg): use setFrameGeometry?
            win->set_frame_geometry(frame_geo);

            auto top_lead = lead_of_annexed_transient(win);
            top_lead->addLayerRepaint(old_frame_geo.united(frame_geo));
            top_lead->discard_quads();

            Q_EMIT win->frame_geometry_changed(win, old_frame_geo);
        }
    };

    set_pos();

    QObject::connect(
        subsurface, &Wrapland::Server::Subsurface::resourceDestroyed, win, &Win::destroy);
    QObject::connect(subsurface, &Wrapland::Server::Subsurface::positionChanged, win, set_pos);
    QObject::connect(lead, &Lead::frame_geometry_changed, win, set_pos);

    win->set_layer(win::layer::unmanaged);

    win->map();
}

}
