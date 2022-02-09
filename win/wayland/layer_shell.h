/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "space.h"
#include "window_release.h"

#include "win/geo.h"
#include "win/screen.h"
#include "win/stacking.h"
#include "win/transient.h"

#include "base/wayland/output.h"
#include "base/wayland/server.h"
#include "input/keyboard_redirect.h"
#include "input/redirect.h"
#include "render/compositor.h"
#include "screens.h"

#include <KScreenLocker/KsldApp>
#include <Wrapland/Server/layer_shell_v1.h>
#include <Wrapland/Server/surface.h>

namespace KWin::win::wayland
{

template<typename Win>
QRectF layer_surface_area(Win* win)
{
    auto layer_surf = win->layer_surface;
    auto output_geo = layer_surf->output()->geometry();

    if (layer_surf->exclusive_zone() == 0) {
        auto output_geo_rect = output_geo.toRect();
        auto area = workspace()->clientArea(WorkArea, output_geo_rect.center(), 0);
        return area.intersected(output_geo_rect);
    }
    return layer_surf->output()->geometry();
}

template<typename Win>
QRect layer_surface_geometry(Win* win)
{
    auto layer_surf = win->layer_surface;
    auto output = layer_surf->output();
    assert(output);

    // TODO(romangg): Instead of using the output geometry we should use some Workspace::clientArea
    //                depending on the layer the surface is on.
    auto const area_geo = layer_surface_area(win);

    auto get_size = [&] {
        auto size = layer_surf->size();
        auto margins = layer_surf->margins();
        if (size.width() == 0) {
            assert(layer_surf->anchor() & Qt::LeftEdge);
            assert(layer_surf->anchor() & Qt::RightEdge);
            size.setWidth(area_geo.width() - margins.left() - margins.right());
        }
        if (size.height() == 0) {
            assert(layer_surf->anchor() & Qt::TopEdge);
            assert(layer_surf->anchor() & Qt::BottomEdge);
            size.setHeight(area_geo.height() - margins.top() - margins.bottom());
        }
        return size;
    };
    auto const set_size = get_size();

    auto get_effective_size = [&] {
        auto size = set_size;

        if (auto const surface_size = win->surface()->size(); !surface_size.isEmpty()) {
            // The client might have committed a surface with different size than the set_size.
            size.setWidth(std::min(set_size.width(), surface_size.width()));
            size.setHeight(std::min(set_size.height(), surface_size.height()));
        }

        // Limit to the output size.
        size.setHeight(std::min(size.height(), static_cast<int>(area_geo.height())));
        size.setWidth(std::min(size.width(), static_cast<int>(area_geo.width())));
        return size;
    };
    auto const eff_size = get_effective_size();

    auto get_pos = [&] {
        auto anchor = layer_surf->anchor();
        auto margins = layer_surf->margins();

        auto x_mid = area_geo.x() + area_geo.width() / 2;
        auto y_mid = area_geo.y() + area_geo.height() / 2;

        // When not anchored to opposite edges or to both we center the surface in this dimension.
        auto x = x_mid - eff_size.width() / 2;
        auto y = y_mid - eff_size.height() / 2;

        if (anchor & Qt::LeftEdge) {
            if (!(anchor & Qt::RightEdge)) {
                // Anchored only left. We use the output position plus margin.
                x = area_geo.x() + margins.left();
            }
        } else if (anchor & Qt::RightEdge) {
            // Only anchored right. We position it relative to right output side minus margin.
            x = area_geo.right() - margins.right() - eff_size.width();
        }
        if (anchor & Qt::TopEdge) {
            if (!(anchor & Qt::BottomEdge)) {
                // Anchored only top. We use the output position plus margin.
                y = area_geo.y() + margins.top();
            }
        } else if (anchor & Qt::BottomEdge) {
            // Only anchored bottom. We position it relative to output bottom minus margin.
            y = area_geo.bottom() - margins.bottom() - eff_size.height();
        }
        return QPoint(x, y);
    };

    return QRect(get_pos(), eff_size);
}

template<typename Win>
void assign_layer_surface_role(Win* win, Wrapland::Server::LayerSurfaceV1* layer_surface)
{
    namespace WS = Wrapland::Server;

    assert(win->surface());
    assert(layer_surface->surface() == win->surface());

    win->control.reset(new control(win));
    win->layer_surface = layer_surface;
    block_geometry_updates(win, true);

    QObject::connect(win, &window::needsRepaint, render::compositor::self(), [win] {
        render::compositor::self()->schedule_repaint(win);
    });
    QObject::connect(
        layer_surface, &WS::LayerSurfaceV1::resourceDestroyed, win, [win] { destroy_window(win); });

    QObject::connect(layer_surface, &WS::LayerSurfaceV1::got_popup, win, [win](auto popup) {
        for (auto window : static_cast<win::wayland::space*>(workspace())->m_windows) {
            if (auto wayland_window = qobject_cast<win::wayland::window*>(window);
                wayland_window && wayland_window->popup == popup) {
                win->transient()->add_child(wayland_window);
                break;
            }
        }
    });

    QObject::connect(layer_surface,
                     &WS::LayerSurfaceV1::configure_acknowledged,
                     win,
                     [win](auto serial) { win->acked_configure = serial; });

    auto handle_first_commit = [win] {
        QObject::disconnect(win->surface(), &WS::Surface::committed, win, nullptr);
        QObject::connect(win->surface(), &WS::Surface::committed, win, &window::handle_commit);

        block_geometry_updates(win, false);

        if (!win->layer_surface->output()) {
            auto current_index = kwinApp()->get_base().screens.current();
            auto output = kwinApp()->get_base().get_outputs().at(current_index);
            win->layer_surface->set_output(
                static_cast<base::wayland::output*>(output)->wrapland_output());
        }

        if (win->pending_configures.empty()) {
            // wlr-layer-shell protocol stipulates a single configure event on first commit.
            process_layer_surface_commit(win);
        }

        win->initialized = true;
    };

    QObject::connect(win->surface(), &WS::Surface::committed, win, [handle_first_commit] {
        handle_first_commit();
    });
}

template<typename Window, typename Space>
void handle_new_layer_surface(Space* space, Wrapland::Server::LayerSurfaceV1* layer_surface)
{
    auto window = new Window(layer_surface->surface());
    if (layer_surface->surface()->client() == space->server->screen_locker_client_connection) {
        ScreenLocker::KSldApp::self()->lockScreenShown();
    }

    space->m_windows.push_back(window);
    QObject::connect(layer_surface,
                     &Wrapland::Server::LayerSurfaceV1::resourceDestroyed,
                     space,
                     [space, window] { remove_all(space->m_windows, window); });

    win::wayland::assign_layer_surface_role(window, layer_surface);

    if (window->readyForPainting()) {
        space->handle_window_added(window);
    } else {
        QObject::connect(
            window, &win::wayland::window::windowShown, space, &Space::handle_wayland_window_shown);
    }
}

template<typename Win>
void layer_surface_handle_keyboard_interactivity(Win* win)
{
    using inter = Wrapland::Server::LayerSurfaceV1::KeyboardInteractivity;

    auto interactivity = win->layer_surface->keyboard_interactivity();
    if (interactivity != inter::OnDemand) {
        // With interactivity None or Exclusive just reset control.
        Workspace::self()->activateNextClient(win);
    }
    kwinApp()->input->redirect->keyboard()->update();
}

template<typename Win>
void layer_surface_handle_exclusive_zone(Win* win)
{
    auto surf = win->layer_surface;
    auto exclusive = surf->exclusive_zone();

    if (exclusive <= 0) {
        // No edge is being reserved.
        return;
    }

    auto anchor = surf->anchor();

    // TODO(romangg): At the moment we reserve only via the clientArea mechanism in Workspace. This
    // is not fully compliant with layer-shell protocol, which allows to reserve space by integer.
    if ((anchor & Qt::LeftEdge) != (anchor & Qt::RightEdge)) {
        // Reference anchor is a vertical edge.
        if (anchor & Qt::LeftEdge) {
            // TODO(romangg): set strut explicitly on left edge.
        } else {
            // TODO(romangg): set strut explicitly on right edge.
        }
    } else {
        // Reference anchor is a horizontal edge.
        if (anchor & Qt::TopEdge) {
            // TODO(romangg): set strut explicitly on top edge.
        } else {
            // TODO(romangg): set strut explicitly on bottom edge.
        }
    }

    // TODO(romangg): Reserver space for screen edge?
}

template<typename Win>
NET::WindowType layer_surface_type(Win* win)
{
    using layer = Wrapland::Server::LayerSurfaceV1::Layer;
    switch (win->layer_surface->layer()) {
    case layer::Background:
        return NET::Desktop;
    case layer::Bottom:
        return NET::Dock;
    case layer::Top:
        return NET::Notification;
    case layer::Overlay:
        return NET::OnScreenDisplay;
    default:
        assert(false);
        return NET::Normal;
    }
}

template<typename Win>
void process_layer_surface_commit(Win* win)
{
    layer_surface_handle_keyboard_interactivity(win);

    auto geo = layer_surface_geometry(win);
    layer_surface_handle_exclusive_zone(win);

    if (win->pending_configures.empty()) {
        win->setFrameGeometry(geo);
    } else {
        for (auto& config : win->pending_configures) {
            if (config.serial == win->acked_configure) {
                config.geometry.frame = geo;
                break;
            }
        }
    }

    if (win->layer_surface->layer() == Wrapland::Server::LayerSurfaceV1::Layer::Bottom) {
        win->control->set_keep_below(true);
        win->window_type = NET::Normal;
    } else {
        win->control->set_keep_below(false);
        win->window_type = layer_surface_type(win);
        if (win->window_type == NET::Desktop || win->window_type == NET::OnScreenDisplay
            || win->window_type == NET::Notification) {
            set_on_all_desktops(win, true);
        }
    }
    update_layer(win);

    // TODO(romangg): update client area also on size change?
    if (win->layer_surface->exclusive_zone() > 0) {
        workspace()->updateClientArea();
    }
}

template<typename Win>
void handle_layer_surface_commit(Win* win)
{
    assert(win->layer_surface);

    if (!win->layer_surface->change_pending()
        && win->geometry_update.frame == win->frameGeometry()) {
        return;
    }

    process_layer_surface_commit(win);
}

}
