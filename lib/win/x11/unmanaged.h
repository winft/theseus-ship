/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "damage.h"
#include "event.h"
#include "meta.h"
#include "window_release.h"
#include "xcb.h"

#include "base/x11/grabs.h"
#include "base/x11/xcb/extensions.h"
#include "base/x11/xcb/proto.h"
#include "win/remnant.h"

namespace KWin::win::x11
{

template<typename Win, typename Space>
Win* find_unmanaged(Space&& space, xcb_window_t xcb_win)
{
    for (auto& var_win : space.windows) {
        if (auto win = std::visit(overload{[xcb_win](Win* win) -> Win* {
                                               if (win->remnant || win->control
                                                   || win->xcb_windows.client != xcb_win) {
                                                   return nullptr;
                                               }
                                               return win;
                                           },
                                           [](auto&& /*win*/) -> Win* { return nullptr; }},
                                  var_win)) {
            return win;
        }
    }

    return nullptr;
}

template<typename Space>
auto create_unmanaged_window(xcb_window_t xcb_win, Space& space) -> typename Space::x11_window*
{
    using Win = typename Space::x11_window;

    using render_t = typename Space::base_t::render_t;
    if constexpr (requires(render_t render) {
                      {
                          render.is_overlay_window(xcb_win)
                      } -> std::same_as<bool>;
                  }) {
        if (space.base.render->is_overlay_window(xcb_win)) {
            return nullptr;
        }
    }

    // Window types that are supported as unmanaged (mainly for compositing).
    window_type_mask const supported_default_types = window_type_mask::normal
        | window_type_mask::desktop | window_type_mask::dock | window_type_mask::toolbar
        | window_type_mask::menu
        | window_type_mask::dialog /*| window_type_mask::override*/ | window_type_mask::top_menu
        | window_type_mask::utility | window_type_mask::splash | window_type_mask::dropdown_menu
        | window_type_mask::popup_menu | window_type_mask::tooltip | window_type_mask::notification
        | window_type_mask::combo_box | window_type_mask::dnd_icon
        | window_type_mask::on_screen_display | window_type_mask::critical_notification;

    auto con = space.base.x11_data.connection;
    base::x11::server_grabber xserverGrabber(con);
    base::x11::xcb::window_attributes attr(con, xcb_win);
    base::x11::xcb::geometry geo(con, xcb_win);

    if (attr.is_null() || attr->map_state != XCB_MAP_STATE_VIEWABLE) {
        return nullptr;
    }
    if (attr->_class == XCB_WINDOW_CLASS_INPUT_ONLY) {
        return nullptr;
    }
    if (geo.is_null()) {
        return nullptr;
    }

    auto win = new Win(xcb_win, space);

    win->supported_default_types = supported_default_types;
    win->topo.layer = layer::unmanaged;

    QTimer::singleShot(50, win->qobject.get(), [win] { set_ready_for_painting(*win); });

    // The window is also the frame.
    base::x11::xcb::select_input(space.base.x11_data.connection,
                                 xcb_win,
                                 attr->your_event_mask | XCB_EVENT_MASK_STRUCTURE_NOTIFY
                                     | XCB_EVENT_MASK_PROPERTY_CHANGE);
    win->geo.frame = geo.rect();
    check_screen(*win);
    win->xcb_visual = attr->visual;
    win->render_data.bit_depth = geo->depth;
    win->net_info = new net::win_info(con,
                                      xcb_win,
                                      win->space.base.x11_data.root_window,
                                      net::WMWindowType | net::WMPid,
                                      net::WM2Opacity | net::WM2WindowRole | net::WM2WindowClass
                                          | net::WM2OpaqueRegion);
    fetch_wm_class(*win);

    // TODO(romangg): Can't chain these two calls, because the second only takes non-const refs.
    auto client_leader_prop = fetch_wm_client_leader(*win);
    read_wm_client_leader(*win, client_leader_prop);

    fetch_wm_client_machine(*win);
    if (base::x11::xcb::extensions::self()->is_shape_available()) {
        xcb_shape_select_input(con, xcb_win, true);
    }
    detect_shape(*win);
    fetch_wm_opaque_region(*win);
    set_skip_close_animation(*win, fetch_skip_close_animation(*win).to_bool());
    win->setupCompositing();

    if (auto internalWindow = find_internal_window(*win)) {
        win->is_outline = internalWindow->property("__kwin_outline").toBool();
    }
    if (auto& effects = space.base.render->effects) {
        effects->checkInputWindowStacking();
    }

    QObject::connect(win->qobject.get(),
                     &Win::qobject_t::needsRepaint,
                     space.base.render->qobject.get(),
                     [win] { win->space.base.render->schedule_repaint(win); });

    space.windows.push_back(win);
    space.stacking.order.render_restack_required = true;
    Q_EMIT space.qobject->unmanagedAdded(win->meta.signal_id);

    return win;
}

template<typename Win>
void unmanaged_configure_event(Win* win, xcb_configure_notify_event_t* event)
{
    if (auto& effects = win->space.base.render->effects) {
        // keep them on top
        effects->checkInputWindowStacking();
    }
    QRect newgeom(event->x, event->y, event->width, event->height);
    if (newgeom != win->geo.frame) {
        // Damage old area.
        win->space.base.render->addRepaint(visible_rect(win));

        auto const old = win->geo.frame;
        win->geo.frame = newgeom;

        add_full_repaint(*win);

        if (old.size() != win->geo.frame.size()) {
            discard_buffer(*win);
        }
        Q_EMIT win->qobject->frame_geometry_changed(old);
    }
}

template<typename Win>
bool unmanaged_event(Win* win, xcb_generic_event_t* event)
{
    auto old_opacity = win->opacity();
    net::Properties dirtyProperties;
    net::Properties2 dirtyProperties2;

    // Pass through the NET stuff.
    win->net_info->event(event, &dirtyProperties, &dirtyProperties2);

    if (dirtyProperties2 & net::WM2Opacity) {
        if (win->space.base.render->scene) {
            add_full_repaint(*win);
            Q_EMIT win->qobject->opacityChanged(old_opacity);
        }
    }
    if (dirtyProperties2 & net::WM2OpaqueRegion) {
        fetch_wm_opaque_region(*win);
    }
    if (dirtyProperties2.testFlag(net::WM2WindowRole)) {
        Q_EMIT win->qobject->windowRoleChanged();
    }
    if (dirtyProperties2.testFlag(net::WM2WindowClass)) {
        fetch_wm_class(*win);
    }

    auto const eventType = event->response_type & ~0x80;
    switch (eventType) {
    case XCB_DESTROY_NOTIFY:
        x11::destroy_window(win);
        break;
    case XCB_UNMAP_NOTIFY: {
        // may cause leave event
        win->space.focusMousePos = win->space.input->cursor->pos();

        // unmap notify might have been emitted due to a destroy notify
        // but unmap notify gets emitted before the destroy notify, nevertheless at this
        // point the window is already destroyed. This means any XCB request with the window
        // will cause an error.
        // To not run into these errors we try to wait for the destroy notify. For this we
        // generate a round trip to the X server and wait a very short time span before
        // handling the release.
        base::x11::update_time_from_clock(win->space.base);

        // using 1 msec to not just move it at the end of the event loop but add an very short
        // timespan to cover cases like unmap() followed by destroy(). The only other way to
        // ensure that the window is not destroyed when we do the release handling is to grab
        // the XServer which we do not want to do for an Unmanaged. The timespan of 1 msec is
        // short enough to not cause problems in the close window animations.
        // It's of course still possible that we miss the destroy in which case non-fatal
        // X errors are reported to the event loop and logged by Qt.
        win->has_scheduled_release = true;
        QTimer::singleShot(1, win->qobject.get(), [win] { release_unmanaged(win, false); });
        break;
    }
    case XCB_CONFIGURE_NOTIFY:
        unmanaged_configure_event(win, reinterpret_cast<xcb_configure_notify_event_t*>(event));
        break;
    case XCB_PROPERTY_NOTIFY:
        property_notify_event_prepare(*win, reinterpret_cast<xcb_property_notify_event_t*>(event));
        break;
    case XCB_CLIENT_MESSAGE:
        handle_wl_surface_id_event(*win, reinterpret_cast<xcb_client_message_event_t*>(event));
        break;
    default: {
        if (eventType == base::x11::xcb::extensions::self()->shape_notify_event()) {
            detect_shape(*win);
            add_full_repaint(*win);

            // In case shape change removes part of this window.
            win->space.base.render->addRepaint(win->geo.frame);

            Q_EMIT win->qobject->frame_geometry_changed(win->geo.frame);
        }
        if (eventType == base::x11::xcb::extensions::self()->damage_notify_event()) {
            damage_handle_notify_event(*win);
        }
        break;
    }
    }
    // Don't eat events, even our own unmanaged widgets are tracked.
    return false;
}

}
