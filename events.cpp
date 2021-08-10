/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

/*

 This file contains things relevant to handling incoming events.

*/

#include "input/cursor.h"
#include "workspace.h"
#include "atoms.h"
#ifdef KWIN_BUILD_TABBOX
#include "tabbox.h"
#endif
#include "rules/rules.h"
#include "useractions.h"
#include "effects.h"
#include "screens.h"
#include "xcbutils.h"

#include "platform/x11/event_filter.h"
#include "platform/x11/event_filter_container.h"
#include "platform/x11/event_filter_manager.h"
#include "win/focuschain.h"
#include "win/input.h"
#include "win/scene.h"
#include "win/x11/event.h"
#include "win/x11/group.h"
#include "win/x11/netinfo.h"
#include "win/x11/stacking_tree.h"
#include "win/x11/unmanaged.h"
#include "win/x11/window.h"

#include <KDecoration2/Decoration>

#include <QApplication>
#include <QDebug>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStyleHints>
#include <QWheelEvent>

#include <kkeyserver.h>

#include <xcb/sync.h>
#ifdef XCB_ICCCM_FOUND
#include <xcb/xcb_icccm.h>
#endif

#include "platform/x11/event_filter.h"

#include "wayland_server.h"
#include <Wrapland/Server/compositor.h>
#include <Wrapland/Server/surface.h>

#ifndef XCB_GE_GENERIC
#define XCB_GE_GENERIC 35
typedef struct xcb_ge_generic_event_t {
    uint8_t  response_type; /**<  */
    uint8_t  extension; /**<  */
    uint16_t sequence; /**<  */
    uint32_t length; /**<  */
    uint16_t event_type; /**<  */
    uint8_t  pad0[22]; /**<  */
    uint32_t full_sequence; /**<  */
} xcb_ge_generic_event_t;
#endif

namespace KWin
{

// ****************************************
// Workspace
// ****************************************

QVector<QByteArray> s_xcbEerrors({
    QByteArrayLiteral("Success"),
    QByteArrayLiteral("BadRequest"),
    QByteArrayLiteral("BadValue"),
    QByteArrayLiteral("BadWindow"),
    QByteArrayLiteral("BadPixmap"),
    QByteArrayLiteral("BadAtom"),
    QByteArrayLiteral("BadCursor"),
    QByteArrayLiteral("BadFont"),
    QByteArrayLiteral("BadMatch"),
    QByteArrayLiteral("BadDrawable"),
    QByteArrayLiteral("BadAccess"),
    QByteArrayLiteral("BadAlloc"),
    QByteArrayLiteral("BadColor"),
    QByteArrayLiteral("BadGC"),
    QByteArrayLiteral("BadIDChoice"),
    QByteArrayLiteral("BadName"),
    QByteArrayLiteral("BadLength"),
    QByteArrayLiteral("BadImplementation"),
    QByteArrayLiteral("Unknown")});

/**
 * Handles workspace specific XCB event
 */
bool Workspace::workspaceEvent(xcb_generic_event_t *e)
{
    const uint8_t eventType = e->response_type & ~0x80;
    if (!eventType) {
        // let's check whether it's an error from one of the extensions KWin uses
        xcb_generic_error_t *error = reinterpret_cast<xcb_generic_error_t*>(e);
        const QVector<Xcb::ExtensionData> extensions = Xcb::Extensions::self()->extensions();
        for (const auto &extension : extensions) {
            if (error->major_code == extension.majorOpcode) {
                QByteArray errorName;
                if (error->error_code < s_xcbEerrors.size()) {
                    errorName = s_xcbEerrors.at(error->error_code);
                } else if (error->error_code >= extension.errorBase) {
                    const int index = error->error_code - extension.errorBase;
                    if (index >= 0 && index < extension.errorCodes.size()) {
                        errorName = extension.errorCodes.at(index);
                    }
                }
                if (errorName.isEmpty()) {
                    errorName = QByteArrayLiteral("Unknown");
                }
                qCWarning(KWIN_CORE, "XCB error: %d (%s), sequence: %d, resource id: %d, major code: %d (%s), minor code: %d (%s)",
                         int(error->error_code), errorName.constData(),
                         int(error->sequence), int(error->resource_id),
                         int(error->major_code), extension.name.constData(),
                         int(error->minor_code),
                         extension.opCodes.size() > error->minor_code ? extension.opCodes.at(error->minor_code).constData() : "Unknown");
                return true;
            }
        }
        return false;
    }

    if (eventType == XCB_GE_GENERIC) {
        xcb_ge_generic_event_t *ge = reinterpret_cast<xcb_ge_generic_event_t *>(e);

        // We need to make a shadow copy of the event filter list because an activated event
        // filter may mutate it by removing or installing another event filter.
        auto const eventFilters = kwinApp()->x11_event_filters->generic_filters;

        for (auto container : eventFilters) {
            if (!container) {
                continue;
            }
            auto filter = container->filter();
            if (filter->extension() == ge->extension && filter->genericEventTypes().contains(ge->event_type) && filter->event(e)) {
                return true;
            }
        }
    } else {
        // We need to make a shadow copy of the event filter list because an activated event
        // filter may mutate it by removing or installing another event filter.
        auto const eventFilters = kwinApp()->x11_event_filters->filters;

        for (auto container : eventFilters) {
            if (!container) {
                continue;
            }
            auto filter = container->filter();
            if (filter->eventTypes().contains(eventType) && filter->event(e)) {
                return true;
            }
        }
    }

    if (effects && static_cast< EffectsHandlerImpl* >(effects)->hasKeyboardGrab()
            && (eventType == XCB_KEY_PRESS || eventType == XCB_KEY_RELEASE))
        return false; // let Qt process it, it'll be intercepted again in eventFilter()

    // events that should be handled before Clients can get them
    switch (eventType) {
    case XCB_CONFIGURE_NOTIFY:
        if (reinterpret_cast<xcb_configure_notify_event_t*>(e)->event == rootWindow())
            x_stacking_tree->mark_as_dirty();
        break;
    };

    auto const eventWindow = win::x11::find_event_window(e);
    if (eventWindow != XCB_WINDOW_NONE) {
        if (auto c = findClient(win::x11::predicate_match::window, eventWindow)) {
            if (win::x11::window_event(c, e)) {
                return true;
            }
        } else if (auto c = findClient(win::x11::predicate_match::wrapper_id, eventWindow)) {
            if (win::x11::window_event(c, e)) {
                return true;
            }
        } else if (auto c = findClient(win::x11::predicate_match::frame_id, eventWindow)) {
            if (win::x11::window_event(c, e)) {
                return true;
            }
        } else if (auto c = findClient(win::x11::predicate_match::input_id, eventWindow)) {
            if (win::x11::window_event(c, e)) {
                return true;
            }
        } else if (auto unmanaged = findUnmanaged(eventWindow)) {
            if (win::x11::unmanaged_event(unmanaged, e)) {
                return true;
            }
        }
    }

    switch (eventType) {
    case XCB_CREATE_NOTIFY: {
        const auto *event = reinterpret_cast<xcb_create_notify_event_t*>(e);
        if (event->parent == rootWindow() &&
                !QWidget::find(event->window) &&
                !event->override_redirect) {
            // see comments for allowClientActivation()
            updateXTime();
            const xcb_timestamp_t t = xTime();
            xcb_change_property(connection(), XCB_PROP_MODE_REPLACE, event->window, atoms->kde_net_wm_user_creation_time, XCB_ATOM_CARDINAL, 32, 1, &t);
        }
        break;
    }
    case XCB_UNMAP_NOTIFY: {
        const auto *event = reinterpret_cast<xcb_unmap_notify_event_t*>(e);
        return (event->event != event->window);   // hide wm typical event from Qt
    }
    case XCB_REPARENT_NOTIFY: {
        //do not confuse Qt with these events. After all, _we_ are the
        //window manager who does the reparenting.
        return true;
    }
    case XCB_MAP_REQUEST: {
        updateXTime();

        const auto *event = reinterpret_cast<xcb_map_request_event_t*>(e);
        if (auto c = findClient(win::x11::predicate_match::window, event->window)) {
            // e->xmaprequest.window is different from e->xany.window
            // TODO this shouldn't be necessary now
            win::x11::window_event(c, e);
            win::FocusChain::self()->update(c, win::FocusChain::Update);
        } else if ( true /*|| e->xmaprequest.parent != root */ ) {
            // NOTICE don't check for the parent being the root window, this breaks when some app unmaps
            // a window, changes something and immediately maps it back, without giving KWin
            // a chance to reparent it back to root
            // since KWin can get MapRequest only for root window children and
            // children of WindowWrapper (=clients), the check is AFAIK useless anyway
            // NOTICE: The save-set support in X11Client::mapRequestEvent() actually requires that
            // this code doesn't check the parent to be root.
            if (!createClient(event->window, false)) {
                xcb_map_window(connection(), event->window);
                const uint32_t values[] = { XCB_STACK_MODE_ABOVE };
                xcb_configure_window(connection(), event->window, XCB_CONFIG_WINDOW_STACK_MODE, values);
            }
        }
        return true;
    }
    case XCB_MAP_NOTIFY: {
        const auto *event = reinterpret_cast<xcb_map_notify_event_t*>(e);
        if (event->override_redirect) {
            auto c = findUnmanaged(event->window);
            if (c == nullptr)
                c = createUnmanaged(event->window);
            if (c) {
                // if hasScheduledRelease is true, it means a unamp and map sequence has occurred. 
                // since release is scheduled after map notify, this old Unmanaged will get released
                // before KWIN has chance to remanage it again. so release it right now.
                if (c->has_scheduled_release) {
                    win::x11::release_unmanaged(c);
                    c = createUnmanaged(event->window);
                }
                if (c) {
                    return win::x11::unmanaged_event(c, e);
                }
            }
        }
        return (event->event != event->window);   // hide wm typical event from Qt
    }

    case XCB_CONFIGURE_REQUEST: {
        const auto *event = reinterpret_cast<xcb_configure_request_event_t*>(e);
        if (event->parent == rootWindow()) {
            uint32_t values[5] = { 0, 0, 0, 0, 0};
            const uint32_t value_mask = event->value_mask
                                        & (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH);
            int i = 0;
            if (value_mask & XCB_CONFIG_WINDOW_X) {
                values[i++] = event->x;
            }
            if (value_mask & XCB_CONFIG_WINDOW_Y) {
                values[i++] = event->y;
            }
            if (value_mask & XCB_CONFIG_WINDOW_WIDTH) {
                values[i++] = event->width;
            }
            if (value_mask & XCB_CONFIG_WINDOW_HEIGHT) {
                values[i++] = event->height;
            }
            if (value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) {
                values[i++] = event->border_width;
            }
            xcb_configure_window(connection(), event->window, value_mask, values);
            return true;
        }
        break;
    }
    case XCB_FOCUS_IN: {
        const auto *event = reinterpret_cast<xcb_focus_in_event_t*>(e);
        if (event->event == rootWindow()
                && (event->detail == XCB_NOTIFY_DETAIL_NONE || event->detail == XCB_NOTIFY_DETAIL_POINTER_ROOT || event->detail == XCB_NOTIFY_DETAIL_INFERIOR)) {
            Xcb::CurrentInput currentInput;
            updateXTime(); // focusToNull() uses xTime(), which is old now (FocusIn has no timestamp)
            // it seems we can "loose" focus reversions when the closing client hold a grab
            // => catch the typical pattern (though we don't want the focus on the root anyway) #348935
            const bool lostFocusPointerToRoot = currentInput->focus == rootWindow() && event->detail == XCB_NOTIFY_DETAIL_INFERIOR;
            if (!currentInput.isNull() && (currentInput->focus == XCB_WINDOW_NONE || currentInput->focus == XCB_INPUT_FOCUS_POINTER_ROOT || lostFocusPointerToRoot)) {
                //kWarning( 1212 ) << "X focus set to None/PointerRoot, reseting focus" ;
                auto window = mostRecentlyActivatedClient();
                if (window != nullptr) {
                    request_focus(window, false, true);
                } else if (activateNextClient(nullptr)) {
                    ; // ok, activated
                } else {
                    focusToNull();
                }
            }
        }
    }
        // fall through
    case XCB_FOCUS_OUT:
        return true; // always eat these, they would tell Qt that KWin is the active app
    default:
        break;
    }
    return false;
}

// Used only to filter events that need to be processed by Qt first
// (e.g. keyboard input to be composed), otherwise events are
// handle by the XEvent filter above
bool Workspace::workspaceEvent(QEvent* e)
{
    if ((e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease || e->type() == QEvent::ShortcutOverride)
            && effects && static_cast< EffectsHandlerImpl* >(effects)->hasKeyboardGrab()) {
        static_cast< EffectsHandlerImpl* >(effects)->grabbedKeyboardEvent(static_cast< QKeyEvent* >(e));
        return true;
    }
    return false;
}

// ****************************************
// Toplevel
// ****************************************

void Toplevel::propertyNotifyEvent(xcb_property_notify_event_t *e)
{
    if (e->window != xcb_window())
        return; // ignore frame/wrapper
    switch(e->atom) {
    default:
        if (e->atom == atoms->wm_client_leader)
            getWmClientLeader();
        else if (e->atom == atoms->kde_net_wm_shadow)
            win::update_shadow(this);
        else if (e->atom == atoms->kde_skip_close_animation)
            getSkipCloseAnimation();
        break;
    }
}

void Toplevel::clientMessageEvent(xcb_client_message_event_t *e)
{
    if (e->type == atoms->wl_surface_id) {
        m_surfaceId = e->data.data32[0];
        if (auto w = waylandServer()) {
            if (auto s = w->compositor()->getSurface(m_surfaceId, w->xWaylandConnection())) {
                setSurface(s);
            }
        }
        emit surfaceIdChanged(m_surfaceId);
    }
}

} // namespace
