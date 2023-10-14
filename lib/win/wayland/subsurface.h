/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "transient.h"
#include "window_release.h"

#include "win/transient.h"
#include <win/wayland/space_windows.h>

#include <Wrapland/Server/subcompositor.h>
#include <Wrapland/Server/surface.h>

namespace KWin::win::wayland
{

template<typename Win>
void assign_subsurface_role(Win* win)
{
    assert(win->surface);
    assert(win->surface->subsurface());

    win->transient->annexed = true;
}

template<typename Win>
void restack_subsurfaces(Win* window)
{
    auto const& subsurfaces = window->surface->state().children;
    auto& children = window->transient->children;

    for (auto const& subsurface : subsurfaces) {
        auto it = std::find_if(children.begin(), children.end(), [&subsurface](auto child) {
            return child->surface == subsurface->surface();
        });
        if (it == children.end()) {
            continue;
        }

        move_to_back(children, *it);
    }

    // Optimize and do that only for the first window up the chain not being annexed.
    if (!window->transient->annexed) {
        window->space.stacking.order.update_order();
    }
}

template<typename Win>
void subsurface_set_pos(Win& win)
{
    auto subsurface = win.surface->subsurface();
    auto lead = win.transient->lead();

    assert(subsurface);
    assert(lead);

    auto get_pos = [lead, subsurface] {
        return win::render_geometry(lead).topLeft() + subsurface->position();
    };

    auto const old_frame_geo = win.geo.frame;
    auto const frame_geo = QRect(get_pos(), win.surface->size());

    if (old_frame_geo == frame_geo) {
        return;
    }

    // TODO(romangg): use setFrameGeometry?
    win.geo.frame = frame_geo;

    // A top lead might not be available when the client has deleted one of the parent
    // surfaces in the tree before this subsurface.
    // TODO(romangg): Instead of checking here on it we could ensure annexed children are
    //                destroyed when the parent window is. This could be complicated though
    //                when destroying while iterating over windows.
    auto top_lead = lead_of_annexed_transient(&win);
    if (top_lead) {
        add_layer_repaint(*top_lead, old_frame_geo.united(frame_geo));
        discard_shape(*top_lead);
    }

    Q_EMIT win.qobject->frame_geometry_changed(old_frame_geo);
}

template<typename Win, typename Lead>
void set_subsurface_parent(Win* win, Lead* lead)
{
    namespace WS = Wrapland::Server;

    assert(!win->transient->lead());
    assert(!contains(lead->transient->children, win));

    lead->transient->add_child(win);
    restack_subsurfaces(lead);

    QObject::connect(win->surface, &WS::Surface::committed, win->qobject.get(), [win] {
        if (win->surface->state().updates & Wrapland::Server::surface_change::size) {
            auto const old_geo = win->geo.frame;
            // TODO(romangg): use setFrameGeometry?
            win->geo.frame = QRect(win->geo.pos(), win->surface->size());
            Q_EMIT win->qobject->frame_geometry_changed(old_geo);
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
                     win->space.base.render->qobject.get(),
                     [win] { win->space.base.render->schedule_repaint(win); });

    subsurface_set_pos(*win);

    QObject::connect(win->surface->subsurface(),
                     &Wrapland::Server::Subsurface::positionChanged,
                     win->qobject.get(),
                     [win] { subsurface_set_pos(*win); });

    QObject::connect(win->surface->subsurface(),
                     &Wrapland::Server::Subsurface::resourceDestroyed,
                     win->qobject.get(),
                     [win] { destroy_window(win); });

    win->topo.layer = layer::unmanaged;
    win->map();
}

template<typename Window, typename Space>
void handle_new_subsurface(Space* space, Wrapland::Server::Subsurface* subsurface)
{
    using var_win = typename Space::window_t;
    auto window = new Window(subsurface->surface(), *space);

    space->windows.push_back(window);
    QObject::connect(subsurface,
                     &Wrapland::Server::Subsurface::resourceDestroyed,
                     space->qobject.get(),
                     [space, window] { remove_all(space->windows, var_win(window)); });

    assign_subsurface_role(window);

    for (auto& win : space->windows) {
        if (std::visit(overload{[&](Window* win) {
                                    if (win->surface != subsurface->parentSurface()) {
                                        return false;
                                    }
                                    win::wayland::set_subsurface_parent(window, win);
                                    if (window->render_data.ready_for_painting) {
                                        space_windows_add(*space, *window);
                                        adopt_transient_children(space, window);
                                    }
                                    return true;
                                },
                                [](auto&&) { return false; }},
                       win)) {
            break;
        }
    }

    // No further processing of the subsurface in space. Must wait till a parent is mapped and
    // subsurface is ready for painting.
}
}
