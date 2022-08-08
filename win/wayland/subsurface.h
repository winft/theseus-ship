/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "transient.h"
#include "window_release.h"

#include "render/compositor.h"
#include "win/transient.h"

#include <Wrapland/Server/subcompositor.h>
#include <Wrapland/Server/surface.h>

namespace KWin::win::wayland
{

template<typename Win>
void assign_subsurface_role(Win* win)
{
    assert(win->surface);
    assert(win->surface->subsurface());

    win->transient()->annexed = true;
}

template<typename Win>
void restack_subsurfaces(Win* window)
{
    auto subsurface_stacker = [&window](std::vector<Toplevel*>& children) {
        auto const& subsurfaces = window->surface->state().children;
        std::vector<Toplevel*> stacking;

        for (auto const& subsurface : subsurfaces) {
            auto surface = subsurface->surface();
            auto it = std::find_if(children.begin(), children.end(), [&surface](Toplevel* child) {
                return child->surface == surface;
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
        window->space.stacking_order->update_order();
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

    QObject::connect(win->surface, &WS::Surface::committed, win->qobject.get(), [win] {
        if (win->surface->state().updates & Wrapland::Server::surface_change::size) {
            auto const old_geo = win->frameGeometry();
            // TODO(romangg): use setFrameGeometry?
            win->set_frame_geometry(QRect(win->pos(), win->surface->size()));
            Q_EMIT win->qobject->frame_geometry_changed(win, old_geo);
        }
        win->handle_commit();
    });

    QObject::connect(lead->qobject.get(), &window_qobject::windowShown, win->qobject.get(), [win] {
        win->map();
    });
    QObject::connect(lead->qobject.get(), &window_qobject::windowHidden, win->qobject.get(), [win] {
        win->unmap();
    });

    // TODO(romangg): Why is that needed again? weston-subsurfaces works without it, but Firefox
    //                stops rendering without this connection.
    QObject::connect(win->qobject.get(),
                     &window_qobject::needsRepaint,
                     win->space.render.qobject.get(),
                     [win] { win->space.render.schedule_repaint(win); });

    auto subsurface = win->surface->subsurface();

    auto get_pos = [lead, subsurface] {
        return win::render_geometry(lead).topLeft() + subsurface->position();
    };
    auto set_pos = [win, get_pos]() {
        auto const old_frame_geo = win->frameGeometry();
        auto const frame_geo = QRect(get_pos(), win->surface->size());

        if (old_frame_geo != frame_geo) {
            // TODO(romangg): use setFrameGeometry?
            win->set_frame_geometry(frame_geo);

            // A top lead might not be available when the client has deleted one of the parent
            // surfaces in the tree before this subsurface.
            // TODO(romangg): Instead of checking here on it we could ensure annexed children are
            //                destroyed when the parent window is. This could be complicated though
            //                when destroying while iterating over windows.
            auto top_lead = lead_of_annexed_transient(win);
            if (top_lead) {
                top_lead->addLayerRepaint(old_frame_geo.united(frame_geo));
                top_lead->discard_quads();
            }

            Q_EMIT win->qobject->frame_geometry_changed(win, old_frame_geo);
        }
    };

    set_pos();

    QObject::connect(subsurface,
                     &Wrapland::Server::Subsurface::resourceDestroyed,
                     win->qobject.get(),
                     [win] { destroy_window(win); });
    QObject::connect(
        subsurface, &Wrapland::Server::Subsurface::positionChanged, win->qobject.get(), set_pos);
    QObject::connect(
        lead->qobject.get(), &Lead::qobject_t::frame_geometry_changed, win->qobject.get(), set_pos);

    win->set_layer(win::layer::unmanaged);

    win->map();
}

template<typename Window, typename Space>
void handle_new_subsurface(Space* space, Wrapland::Server::Subsurface* subsurface)
{
    auto window = new Window(subsurface->surface(), *space);

    space->windows.push_back(window);
    QObject::connect(subsurface,
                     &Wrapland::Server::Subsurface::resourceDestroyed,
                     space->qobject.get(),
                     [space, window] { remove_all(space->windows, window); });

    assign_subsurface_role(window);

    for (auto& win : space->windows) {
        if (win->surface == subsurface->parentSurface()) {
            win::wayland::set_subsurface_parent(window, win);
            if (window->ready_for_painting) {
                space->handle_window_added(window);
                adopt_transient_children(space, window);
                return;
            }
            break;
        }
    }

    // No further processing of the subsurface in space. Must wait till a parent is mapped and
    // subsurface is ready for painting.
}

}
