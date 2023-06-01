/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control_create.h"
#include "event.h"
#include "unmanaged.h"

#include "base/x11/event_filter.h"
#include "base/x11/event_filter_container.h"
#include "base/x11/event_filter_manager.h"
#include "base/x11/xcb/proto.h"

#include <string>
#include <vector>

#ifndef XCB_GE_GENERIC
#define XCB_GE_GENERIC 35
typedef struct xcb_ge_generic_event_t {
    uint8_t response_type;  /**<  */
    uint8_t extension;      /**<  */
    uint16_t sequence;      /**<  */
    uint32_t length;        /**<  */
    uint16_t event_type;    /**<  */
    uint8_t pad0[22];       /**<  */
    uint32_t full_sequence; /**<  */
} xcb_ge_generic_event_t;
#endif

namespace KWin::win::x11
{

static std::vector<std::string> const xcb_errors({
    "Success",   "BadRequest", "BadValue",          "BadWindow", "BadPixmap",
    "BadAtom",   "BadCursor",  "BadFont",           "BadMatch",  "BadDrawable",
    "BadAccess", "BadAlloc",   "BadColor",          "BadGC",     "BadIDChoice",
    "BadName",   "BadLength",  "BadImplementation", "Unknown",
});

template<typename Space>
bool space_event(Space& space, xcb_generic_event_t* event)
{
    uint8_t const event_type = event->response_type & ~0x80;
    if (!event_type) {
        // let's check whether it's an error from one of the extensions KWin uses
        auto error = reinterpret_cast<xcb_generic_error_t*>(event);
        auto const extensions = base::x11::xcb::extensions::self()->get_data();

        for (auto const& extension : extensions) {
            if (error->major_code == extension.majorOpcode) {
                QByteArray errorName;

                if (error->error_code < xcb_errors.size()) {
                    errorName = xcb_errors.at(error->error_code).c_str();
                } else if (error->error_code >= extension.errorBase) {
                    const int index = error->error_code - extension.errorBase;
                    if (index >= 0 && index < extension.errorCodes.size()) {
                        errorName = extension.errorCodes.at(index);
                    }
                }

                if (errorName.isEmpty()) {
                    errorName = QByteArrayLiteral("Unknown");
                }

                qCWarning(KWIN_CORE,
                          "XCB error: %d (%s), sequence: %d, resource id: %d, major code: %d (%s), "
                          "minor code: %d (%s)",
                          int(error->error_code),
                          errorName.constData(),
                          int(error->sequence),
                          int(error->resource_id),
                          int(error->major_code),
                          extension.name.constData(),
                          int(error->minor_code),
                          extension.opCodes.size() > error->minor_code
                              ? extension.opCodes.at(error->minor_code).constData()
                              : "Unknown");
                return true;
            }
        }

        return false;
    }

    if (event_type == XCB_GE_GENERIC) {
        auto gen_event = reinterpret_cast<xcb_ge_generic_event_t*>(event);

        // We need to make a shadow copy of the event filter list because an activated event
        // filter may mutate it by removing or installing another event filter.
        auto const event_filters = space.base.x11_event_filters->generic_filters;

        for (auto container : event_filters) {
            if (!container) {
                continue;
            }
            auto filter = container->filter();
            if (filter->extension() == gen_event->extension
                && filter->genericEventTypes().contains(gen_event->event_type)
                && filter->event(event)) {
                return true;
            }
        }
    } else {
        // We need to make a shadow copy of the event filter list because an activated event
        // filter may mutate it by removing or installing another event filter.
        auto const event_filters = space.base.x11_event_filters->filters;

        for (auto container : event_filters) {
            if (!container) {
                continue;
            }
            auto filter = container->filter();
            if (filter->eventTypes().contains(event_type) && filter->event(event)) {
                return true;
            }
        }
    }

    // events that should be handled before Clients can get them
    switch (event_type) {
    case XCB_CONFIGURE_NOTIFY:
        if (reinterpret_cast<xcb_configure_notify_event_t*>(event)->event
            == space.base.x11_data.root_window) {
            space.stacking.order.render_restack_required = true;
        }
        break;
    };

    using x11_window = typename Space::x11_window;

    auto const event_window = win::x11::find_event_window(event);
    if (event_window != XCB_WINDOW_NONE) {
        if (auto c
            = find_controlled_window<x11_window>(space, predicate_match::window, event_window)) {
            if (win::x11::window_event(c, event)) {
                return true;
            }
        } else if (auto c = find_controlled_window<x11_window>(
                       space, predicate_match::wrapper_id, event_window)) {
            if (win::x11::window_event(c, event)) {
                return true;
            }
        } else if (auto c = find_controlled_window<x11_window>(
                       space, predicate_match::frame_id, event_window)) {
            if (win::x11::window_event(c, event)) {
                return true;
            }
        } else if (auto c = find_controlled_window<x11_window>(
                       space, predicate_match::input_id, event_window)) {
            if (win::x11::window_event(c, event)) {
                return true;
            }
        } else if (auto unmanaged = find_unmanaged<x11_window>(space, event_window)) {
            if (win::x11::unmanaged_event(unmanaged, event)) {
                return true;
            }
        }
    }

    switch (event_type) {
    case XCB_CREATE_NOTIFY: {
        auto create_event = reinterpret_cast<xcb_create_notify_event_t*>(event);
        if (create_event->parent == space.base.x11_data.root_window
            && !QWidget::find(create_event->window) && !create_event->override_redirect) {
            // see comments for allowClientActivation()
            base::x11::update_time_from_clock(space.base);
            auto const t = space.base.x11_data.time;
            xcb_change_property(space.base.x11_data.connection,
                                XCB_PROP_MODE_REPLACE,
                                create_event->window,
                                space.atoms->kde_net_wm_user_creation_time,
                                XCB_ATOM_CARDINAL,
                                32,
                                1,
                                &t);
        }
        break;
    }

    case XCB_UNMAP_NOTIFY: {
        auto unmap_event = reinterpret_cast<xcb_unmap_notify_event_t*>(event);

        // hide wm typical event from Qt
        return (unmap_event->event != unmap_event->window);
    }

    case XCB_REPARENT_NOTIFY: {
        // do not confuse Qt with these events. After all, _we_ are the
        // window manager who does the reparenting.
        return true;
    }

    case XCB_MAP_REQUEST: {
        base::x11::update_time_from_clock(space.base);
        auto map_req_event = reinterpret_cast<xcb_map_request_event_t*>(event);

        if (auto c = find_controlled_window<x11_window>(
                space, predicate_match::window, map_req_event->window)) {
            // event->xmaprequest.window is different from event->xany.window
            // TODO this shouldn't be necessary now
            win::x11::window_event(c, event);
            focus_chain_update(space.stacking.focus_chain, c, focus_chain_change::update);
        } else if (true /*|| event->xmaprequest.parent != root */) {
            // NOTICE don't check for the parent being the root window, this breaks when some app
            // unmaps a window, changes something and immediately maps it back, without giving KWin
            // a chance to reparent it back to root
            // since KWin can get MapRequest only for root window children and
            // children of WindowWrapper (=clients), the check is AFAIK useless anyway
            // NOTICE: The save-set support in X11Client::mapRequestEvent() actually requires that
            // this code doesn't check the parent to be root.
            if (!create_controlled_window(map_req_event->window, false, space)) {
                xcb_map_window(space.base.x11_data.connection, map_req_event->window);
                const uint32_t values[] = {XCB_STACK_MODE_ABOVE};
                xcb_configure_window(space.base.x11_data.connection,
                                     map_req_event->window,
                                     XCB_CONFIG_WINDOW_STACK_MODE,
                                     values);
            }
        }

        return true;
    }

    case XCB_MAP_NOTIFY: {
        auto map_event = reinterpret_cast<xcb_map_notify_event_t*>(event);

        if (map_event->override_redirect) {
            auto c = find_unmanaged<x11_window>(space, map_event->window);
            if (c == nullptr) {
                c = create_unmanaged_window(map_event->window, space);
            }

            if (c) {
                // if hasScheduledRelease is true, it means a unamp and map sequence has occurred.
                // since release is scheduled after map notify, this old Unmanaged will get released
                // before KWIN has chance to remanage it again. so release it right now.
                if (c->has_scheduled_release) {
                    win::x11::release_window(c, false);
                    c = create_unmanaged_window(map_event->window, space);
                }
                if (c) {
                    return win::x11::unmanaged_event(c, event);
                }
            }
        }

        // hide wm typical event from Qt
        return (map_event->event != map_event->window);
    }

    case XCB_CONFIGURE_REQUEST: {
        auto cfg_req_event = reinterpret_cast<xcb_configure_request_event_t*>(event);

        if (cfg_req_event->parent == space.base.x11_data.root_window) {
            uint32_t values[5] = {0, 0, 0, 0, 0};
            const uint32_t value_mask = cfg_req_event->value_mask
                & (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH
                   | XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH);
            int i = 0;
            if (value_mask & XCB_CONFIG_WINDOW_X) {
                values[i++] = cfg_req_event->x;
            }
            if (value_mask & XCB_CONFIG_WINDOW_Y) {
                values[i++] = cfg_req_event->y;
            }
            if (value_mask & XCB_CONFIG_WINDOW_WIDTH) {
                values[i++] = cfg_req_event->width;
            }
            if (value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
                values[i++] = cfg_req_event->height;
            }
            if (value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) {
                values[i++] = cfg_req_event->border_width;
            }
            xcb_configure_window(
                space.base.x11_data.connection, cfg_req_event->window, value_mask, values);
            return true;
        }

        break;
    }

    case XCB_FOCUS_IN: {
        auto focus_event = reinterpret_cast<xcb_focus_in_event_t*>(event);
        if (focus_event->event == space.base.x11_data.root_window
            && (focus_event->detail == XCB_NOTIFY_DETAIL_NONE
                || focus_event->detail == XCB_NOTIFY_DETAIL_POINTER_ROOT
                || focus_event->detail == XCB_NOTIFY_DETAIL_INFERIOR)) {
            base::x11::xcb::input_focus currentInput(space.base.x11_data.connection);

            // focusToNull() uses xTime(), which is old now (FocusIn has no timestamp)
            base::x11::update_time_from_clock(space.base);

            // it seems we can "loose" focus reversions when the closing client hold a grab
            // => catch the typical pattern (though we don't want the focus on the root anyway)
            // #348935
            const bool lostFocusPointerToRoot
                = currentInput->focus == space.base.x11_data.root_window
                && focus_event->detail == XCB_NOTIFY_DETAIL_INFERIOR;
            if (!currentInput.is_null()
                && (currentInput->focus == XCB_WINDOW_NONE
                    || currentInput->focus == XCB_INPUT_FOCUS_POINTER_ROOT
                    || lostFocusPointerToRoot)) {
                // kWarning( 1212 ) << "X focus set to None/PointerRoot, reseting focus" ;
                if (auto act = most_recently_activated_window(space)) {
                    std::visit(
                        overload{[&](auto&& act) { request_focus(space, *act, false, true); }},
                        *act);
                } else if (!activate_next_window(space)) {
                    focus_to_null(space);
                }
            }
        }
    }
        // fall through

    case XCB_FOCUS_OUT:
        // always eat these, they would tell Qt that KWin is the active app
        return true;

    default:
        break;
    }

    return false;
}

template<typename Base>
void update_time_from_event(Base& base, xcb_generic_event_t* event)
{
    xcb_timestamp_t time = XCB_TIME_CURRENT_TIME;
    uint8_t const eventType = event->response_type & ~0x80;

    switch (eventType) {
    case XCB_KEY_PRESS:
    case XCB_KEY_RELEASE:
        time = reinterpret_cast<xcb_key_press_event_t*>(event)->time;
        break;
    case XCB_BUTTON_PRESS:
    case XCB_BUTTON_RELEASE:
        time = reinterpret_cast<xcb_button_press_event_t*>(event)->time;
        break;
    case XCB_MOTION_NOTIFY:
        time = reinterpret_cast<xcb_motion_notify_event_t*>(event)->time;
        break;
    case XCB_ENTER_NOTIFY:
    case XCB_LEAVE_NOTIFY:
        time = reinterpret_cast<xcb_enter_notify_event_t*>(event)->time;
        break;
    case XCB_FOCUS_IN:
    case XCB_FOCUS_OUT:
    case XCB_KEYMAP_NOTIFY:
    case XCB_EXPOSE:
    case XCB_GRAPHICS_EXPOSURE:
    case XCB_NO_EXPOSURE:
    case XCB_VISIBILITY_NOTIFY:
    case XCB_CREATE_NOTIFY:
    case XCB_DESTROY_NOTIFY:
    case XCB_UNMAP_NOTIFY:
    case XCB_MAP_NOTIFY:
    case XCB_MAP_REQUEST:
    case XCB_REPARENT_NOTIFY:
    case XCB_CONFIGURE_NOTIFY:
    case XCB_CONFIGURE_REQUEST:
    case XCB_GRAVITY_NOTIFY:
    case XCB_RESIZE_REQUEST:
    case XCB_CIRCULATE_NOTIFY:
    case XCB_CIRCULATE_REQUEST:
        // no timestamp
        return;
    case XCB_PROPERTY_NOTIFY:
        time = reinterpret_cast<xcb_property_notify_event_t*>(event)->time;
        break;
    case XCB_SELECTION_CLEAR:
        time = reinterpret_cast<xcb_selection_clear_event_t*>(event)->time;
        break;
    case XCB_SELECTION_REQUEST:
        time = reinterpret_cast<xcb_selection_request_event_t*>(event)->time;
        break;
    case XCB_SELECTION_NOTIFY:
        time = reinterpret_cast<xcb_selection_notify_event_t*>(event)->time;
        break;
    case XCB_COLORMAP_NOTIFY:
    case XCB_CLIENT_MESSAGE:
    case XCB_MAPPING_NOTIFY:
    case XCB_GE_GENERIC:
        // no timestamp
        return;
    default:
        // extension handling
        if (base::x11::xcb::extensions::self()) {
            if (eventType == base::x11::xcb::extensions::self()->shape_notify_event()) {
                time = reinterpret_cast<xcb_shape_notify_event_t*>(event)->server_time;
            }
            if (eventType == base::x11::xcb::extensions::self()->damage_notify_event()) {
                time = reinterpret_cast<xcb_damage_notify_event_t*>(event)->timestamp;
            }
        }
        break;
    }

    base::x11::advance_time(base.x11_data, time);
}

}
