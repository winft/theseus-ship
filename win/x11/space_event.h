/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "event.h"
#include "unmanaged.h"

#include "base/x11/event_filter.h"
#include "base/x11/event_filter_container.h"
#include "base/x11/event_filter_manager.h"
#include "base/x11/xcb/proto.h"

#include <string>
#include <vector>

namespace KWin::win::x11
{

// TODO(romangg): make this constexpr with C++20.
static std::vector<std::string> xcb_errors({"Success",
                                            "BadRequest",
                                            "BadValue",
                                            "BadWindow",
                                            "BadPixmap",
                                            "BadAtom",
                                            "BadCursor",
                                            "BadFont",
                                            "BadMatch",
                                            "BadDrawable",
                                            "BadAccess",
                                            "BadAlloc",
                                            "BadColor",
                                            "BadGC",
                                            "BadIDChoice",
                                            "BadName",
                                            "BadLength",
                                            "BadImplementation",
                                            "Unknown"});

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
        auto const event_filters = kwinApp()->x11_event_filters->generic_filters;

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
        auto const event_filters = kwinApp()->x11_event_filters->filters;

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

    if (auto& effects = space.render.effects; effects && effects->hasKeyboardGrab()
        && (event_type == XCB_KEY_PRESS || event_type == XCB_KEY_RELEASE))
        return false; // let Qt process it, it'll be intercepted again in eventFilter()

    // events that should be handled before Clients can get them
    switch (event_type) {
    case XCB_CONFIGURE_NOTIFY:
        if (reinterpret_cast<xcb_configure_notify_event_t*>(event)->event == rootWindow()) {
            space.stacking_order->render_restack_required = true;
        }
        break;
    };

    auto const event_window = win::x11::find_event_window(event);
    if (event_window != XCB_WINDOW_NONE) {
        if (auto c
            = find_controlled_window<x11::window>(space, predicate_match::window, event_window)) {
            if (win::x11::window_event(c, event)) {
                return true;
            }
        } else if (auto c = find_controlled_window<x11::window>(
                       space, predicate_match::wrapper_id, event_window)) {
            if (win::x11::window_event(c, event)) {
                return true;
            }
        } else if (auto c = find_controlled_window<x11::window>(
                       space, predicate_match::frame_id, event_window)) {
            if (win::x11::window_event(c, event)) {
                return true;
            }
        } else if (auto c = find_controlled_window<x11::window>(
                       space, predicate_match::input_id, event_window)) {
            if (win::x11::window_event(c, event)) {
                return true;
            }
        } else if (auto unmanaged = find_unmanaged<win::x11::window>(space, event_window)) {
            if (win::x11::unmanaged_event(unmanaged, event)) {
                return true;
            }
        }
    }

    switch (event_type) {
    case XCB_CREATE_NOTIFY: {
        auto create_event = reinterpret_cast<xcb_create_notify_event_t*>(event);
        if (create_event->parent == rootWindow() && !QWidget::find(create_event->window)
            && !create_event->override_redirect) {
            // see comments for allowClientActivation()
            kwinApp()->update_x11_time_from_clock();
            const xcb_timestamp_t t = xTime();
            xcb_change_property(connection(),
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
        kwinApp()->update_x11_time_from_clock();
        auto map_req_event = reinterpret_cast<xcb_map_request_event_t*>(event);

        if (auto c = find_controlled_window<x11::window>(
                space, predicate_match::window, map_req_event->window)) {
            // event->xmaprequest.window is different from event->xany.window
            // TODO this shouldn't be necessary now
            win::x11::window_event(c, event);
            space.focus_chain.update(c, focus_chain_change::update);
        } else if (true /*|| event->xmaprequest.parent != root */) {
            // NOTICE don't check for the parent being the root window, this breaks when some app
            // unmaps a window, changes something and immediately maps it back, without giving KWin
            // a chance to reparent it back to root
            // since KWin can get MapRequest only for root window children and
            // children of WindowWrapper (=clients), the check is AFAIK useless anyway
            // NOTICE: The save-set support in X11Client::mapRequestEvent() actually requires that
            // this code doesn't check the parent to be root.
            if (!create_controlled_window(map_req_event->window, false, space)) {
                xcb_map_window(connection(), map_req_event->window);
                const uint32_t values[] = {XCB_STACK_MODE_ABOVE};
                xcb_configure_window(
                    connection(), map_req_event->window, XCB_CONFIG_WINDOW_STACK_MODE, values);
            }
        }

        return true;
    }

    case XCB_MAP_NOTIFY: {
        auto map_event = reinterpret_cast<xcb_map_notify_event_t*>(event);

        if (map_event->override_redirect) {
            auto c = find_unmanaged<win::x11::window>(space, map_event->window);
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

        if (cfg_req_event->parent == rootWindow()) {
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
            xcb_configure_window(connection(), cfg_req_event->window, value_mask, values);
            return true;
        }

        break;
    }

    case XCB_FOCUS_IN: {
        auto focus_event = reinterpret_cast<xcb_focus_in_event_t*>(event);
        if (focus_event->event == rootWindow()
            && (focus_event->detail == XCB_NOTIFY_DETAIL_NONE
                || focus_event->detail == XCB_NOTIFY_DETAIL_POINTER_ROOT
                || focus_event->detail == XCB_NOTIFY_DETAIL_INFERIOR)) {
            base::x11::xcb::input_focus currentInput;

            // focusToNull() uses xTime(), which is old now (FocusIn has no timestamp)
            kwinApp()->update_x11_time_from_clock();

            // it seems we can "loose" focus reversions when the closing client hold a grab
            // => catch the typical pattern (though we don't want the focus on the root anyway)
            // #348935
            const bool lostFocusPointerToRoot = currentInput->focus == rootWindow()
                && focus_event->detail == XCB_NOTIFY_DETAIL_INFERIOR;
            if (!currentInput.is_null()
                && (currentInput->focus == XCB_WINDOW_NONE
                    || currentInput->focus == XCB_INPUT_FOCUS_POINTER_ROOT
                    || lostFocusPointerToRoot)) {
                // kWarning( 1212 ) << "X focus set to None/PointerRoot, reseting focus" ;
                auto window = space.mostRecentlyActivatedClient();
                if (window != nullptr) {
                    space.request_focus(window, false, true);
                } else if (space.activateNextClient(nullptr)) {
                    ; // ok, activated
                } else {
                    space.focusToNull();
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

}
