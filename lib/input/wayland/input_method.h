/*
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/wayland/server.h"
#include "input/filters/keyboard_grab.h"
#include "input/xkb/helpers.h"
#include "win/transient.h"
#include "win/wayland/popup_placement.h"
#include "win/wayland/window_release.h"
#include "win/window_area.h"
#include <win/wayland/space_windows.h>

#include <QObject>
#include <Wrapland/Server/display.h>
#include <Wrapland/Server/input_method_v2.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/surface.h>
#include <Wrapland/Server/text_input_pool.h>
#include <Wrapland/Server/text_input_v3.h>
#include <Wrapland/Server/xdg_shell_surface.h>
#include <memory>
#include <vector>

namespace KWin::input::wayland
{

template<typename Redirect>
class input_method : public QObject
{
public:
    using space_t = typename Redirect::space_t;
    using window_t = typename space_t::wayland_window;
    using im_keyboard_grab_v2
        = keyboard_grab<Redirect, Wrapland::Server::input_method_keyboard_grab_v2>;

    input_method(Redirect& redirect)
        : redirect{redirect}
    {
        auto& server = redirect.platform.base.server;
        auto seat = server->seat();

        text_input_manager_v3
            = std::make_unique<Wrapland::Server::text_input_manager_v3>(server->display.get());
        input_method_manager_v2
            = std::make_unique<Wrapland::Server::input_method_manager_v2>(server->display.get());

        QObject::connect(seat,
                         &Wrapland::Server::Seat::input_method_v2_changed,
                         this,
                         &input_method::input_method_v2_changed);
        QObject::connect(seat,
                         &Wrapland::Server::Seat::text_input_v3_enabled_changed,
                         this,
                         [this](auto enabled) {
                             if (enabled) {
                                 activate_filters();
                                 activate_popups();
                             } else {
                                 deactivate();
                             }
                         });
    }

private:
    window_t* get_window(Wrapland::Server::text_input_v3* text_input)
    {
        auto input_surface = text_input->entered_surface();

        for (auto win : redirect.space.windows) {
            if (std::visit(overload{[&](window_t* win) {
                                        return win->control && win->surface == input_surface;
                                    },
                                    [](auto&&) { return false; }},
                           win)) {
                return std::get<window_t*>(win);
            }
        }

        assert(false);
        return nullptr;
    }

    template<typename Win>
    QRect get_input_popup_placement(Win* parent_window, QRect const& cursor_rectangle)
    {
        using constraint_adjust = Wrapland::Server::XdgShellSurface::ConstraintAdjustment;

        auto const toplevel = win::lead_of_annexed_transient(parent_window);
        auto const& screen_bounds
            = win::space_window_area(redirect.space,
                                     toplevel->control->fullscreen ? win::area_option::fullscreen
                                                                   : win::area_option::placement,
                                     toplevel);

        auto const& text_area = cursor_rectangle.isValid() ? cursor_rectangle : QRect(0, 0, 0, 0);

        // Which size should we use? Let's use the same size as the text area.
        auto const& size = text_area.size();

        // Ideally, these depend on the direction of text input.
        auto const& anchor_edge = Qt::BottomEdge | Qt::RightEdge;
        auto const& gravity = Qt::BottomEdge | Qt::RightEdge;

        auto const adjustments
            = constraint_adjust::ResizeX | constraint_adjust::SlideX | constraint_adjust::FlipY;

        return win::wayland::get_popup_placement<Win>({parent_window,
                                                       screen_bounds,
                                                       text_area,
                                                       anchor_edge,
                                                       gravity,
                                                       size,
                                                       QPoint(),
                                                       adjustments});
    }

    void input_method_v2_changed()
    {
        QObject::disconnect(notifiers.popup_created);
        QObject::disconnect(notifiers.keyboard_grabbed);

        popups.clear();
        filters.clear();

        if (auto device = redirect.platform.base.server->seat()->get_input_method_v2()) {
            notifiers.popup_created
                = QObject::connect(device,
                                   &Wrapland::Server::input_method_v2::popup_surface_created,
                                   this,
                                   &input_method::handle_popup_surface_created);
            notifiers.keyboard_grabbed
                = QObject::connect(device,
                                   &Wrapland::Server::input_method_v2::keyboard_grabbed,
                                   this,
                                   &input_method::handle_keyboard_grabbed);
        }
    }

    void handle_keyboard_grabbed(Wrapland::Server::input_method_keyboard_grab_v2* grab)
    {
        auto xkb = xkb::get_primary_xkb_keyboard(redirect.platform);
        auto filter
            = filters.emplace_back(new im_keyboard_grab_v2(redirect, grab, xkb->keymap->raw)).get();

        QObject::connect(grab,
                         &Wrapland::Server::input_method_keyboard_grab_v2::resourceDestroyed,
                         redirect.qobject.get(),
                         [this, filter] {
                             redirect.uninstallInputEventFilter(filter);
                             remove_all_if(filters,
                                           [filter](auto&& f) { return f.get() == filter; });
                         });

        if (auto ti3 = redirect.platform.base.server->seat()->text_inputs().v3.text_input;
            ti3 && ti3->state().enabled) {
            redirect.append_filter(filter);
        }
    }

    void
    handle_popup_surface_created(Wrapland::Server::input_method_popup_surface_v2* popup_surface)
    {
        auto popup = popups.emplace_back(new window_t(popup_surface->surface(), redirect.space));
        popup->input_method_popup = popup_surface;
        popup->transient->annexed = true;
        popup->hidden = true;
        popup->topo.layer = win::layer::notification;

        redirect.space.windows.push_back(popup);

        QObject::connect(popup->qobject.get(), &window_t::qobject_t::closed, this, [this, popup] {
            remove_all(popups, popup);
        });

        QObject::connect(popup_surface,
                         &Wrapland::Server::input_method_popup_surface_v2::resourceDestroyed,
                         popup->qobject.get(),
                         [popup] { win::wayland::destroy_window(popup); });

        QObject::connect(popup->surface,
                         &Wrapland::Server::Surface::committed,
                         popup->qobject.get(),
                         [popup] { popup->handle_commit(); });
        QObject::connect(popup->qobject.get(),
                         &window_t::qobject_t::needsRepaint,
                         redirect.platform.base.mod.render->qobject.get(),
                         [popup] { popup->space.base.mod.render->schedule_repaint(popup); });
        QObject::connect(popup->qobject.get(),
                         &window_t::qobject_t::frame_geometry_changed,
                         popup->qobject.get(),
                         [popup](auto old_frame_geo) {
                             if (!popup->transient->lead()) {
                                 return;
                             }

                             auto const& old_visible_geo = win::visible_rect(popup, old_frame_geo);
                             auto const& visible_geo = win::visible_rect(popup, popup->geo.frame);

                             win::add_layer_repaint(*win::lead_of_annexed_transient(popup),
                                                    old_visible_geo.united(visible_geo));

                             if (old_visible_geo.size() != visible_geo.size()) {
                                 win::discard_shape(*popup);
                             }
                         });

        if (popup->render_data.ready_for_painting) {
            win::wayland::space_windows_add(redirect.space, *popup);
        }

        if (auto text_input = redirect.platform.base.server->seat()->text_inputs().v3.text_input) {
            if (text_input->state().enabled) {
                auto parent_window = get_window(text_input);
                auto const& placement = get_input_popup_placement(
                    parent_window, text_input->state().cursor_rectangle);

                parent_window->transient->add_child(popup);
                popup->setFrameGeometry(placement);
                popup->hideClient(false);
            }
        }
    }

    void activate_filters()
    {
        for (auto const& filter : filters) {
            redirect.append_filter(filter.get());
        }
    }

    void activate_popups()
    {
        if (popups.empty()) {
            return;
        }

        auto text_input = redirect.platform.base.server->seat()->text_inputs().v3.text_input;
        auto parent_window = get_window(text_input);
        auto const placement
            = get_input_popup_placement(parent_window, text_input->state().cursor_rectangle);

        for (auto const& popup : popups) {
            parent_window->transient->add_child(popup);
            popup->setFrameGeometry(placement);
            popup->hideClient(false);
        }
    }

    void deactivate()
    {
        for (auto const& filter : filters) {
            redirect.uninstallInputEventFilter(filter.get());
        }
        for (auto const& popup : popups) {
            popup->hideClient(true);
            popup->transient->lead()->transient->remove_child(popup);
        }
    }

    struct {
        QMetaObject::Connection popup_created;
        QMetaObject::Connection keyboard_grabbed;
    } notifiers;

    std::vector<window_t*> popups;
    std::vector<std::unique_ptr<im_keyboard_grab_v2>> filters;

    std::unique_ptr<Wrapland::Server::text_input_manager_v3> text_input_manager_v3;
    std::unique_ptr<Wrapland::Server::input_method_manager_v2> input_method_manager_v2;

    Redirect& redirect;
};

}
