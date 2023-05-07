/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "actions.h"
#include "activation.h"
#include "appmenu.h"
#include "client.h"
#include "damage.h"
#include "focus_stealing.h"
#include "geo.h"
#include "meta.h"
#include "stacking.h"
#include "transient.h"
#include "user_time.h"
#include "window_release.h"
#include "xcb.h"

#include "base/x11/xcb/extensions.h"
#include "base/x11/xcb/qt_types.h"
#include "render/types.h"
#include "win/activation.h"
#include "win/deco_input.h"
#include "win/desktop_space.h"
#include "win/meta.h"

#include <xcb/damage.h>

namespace KWin::win::x11
{

static inline xcb_window_t find_event_window(xcb_generic_event_t* event)
{
    const uint8_t eventType = event->response_type & ~0x80;
    switch (eventType) {
    case XCB_KEY_PRESS:
    case XCB_KEY_RELEASE:
        return reinterpret_cast<xcb_key_press_event_t*>(event)->event;
    case XCB_BUTTON_PRESS:
    case XCB_BUTTON_RELEASE:
        return reinterpret_cast<xcb_button_press_event_t*>(event)->event;
    case XCB_MOTION_NOTIFY:
        return reinterpret_cast<xcb_motion_notify_event_t*>(event)->event;
    case XCB_ENTER_NOTIFY:
    case XCB_LEAVE_NOTIFY:
        return reinterpret_cast<xcb_enter_notify_event_t*>(event)->event;
    case XCB_FOCUS_IN:
    case XCB_FOCUS_OUT:
        return reinterpret_cast<xcb_focus_in_event_t*>(event)->event;
    case XCB_EXPOSE:
        return reinterpret_cast<xcb_expose_event_t*>(event)->window;
    case XCB_GRAPHICS_EXPOSURE:
        return reinterpret_cast<xcb_graphics_exposure_event_t*>(event)->drawable;
    case XCB_NO_EXPOSURE:
        return reinterpret_cast<xcb_no_exposure_event_t*>(event)->drawable;
    case XCB_VISIBILITY_NOTIFY:
        return reinterpret_cast<xcb_visibility_notify_event_t*>(event)->window;
    case XCB_CREATE_NOTIFY:
        return reinterpret_cast<xcb_create_notify_event_t*>(event)->window;
    case XCB_DESTROY_NOTIFY:
        return reinterpret_cast<xcb_destroy_notify_event_t*>(event)->window;
    case XCB_UNMAP_NOTIFY:
        return reinterpret_cast<xcb_unmap_notify_event_t*>(event)->window;
    case XCB_MAP_NOTIFY:
        return reinterpret_cast<xcb_map_notify_event_t*>(event)->window;
    case XCB_MAP_REQUEST:
        return reinterpret_cast<xcb_map_request_event_t*>(event)->window;
    case XCB_REPARENT_NOTIFY:
        return reinterpret_cast<xcb_reparent_notify_event_t*>(event)->window;
    case XCB_CONFIGURE_NOTIFY:
        return reinterpret_cast<xcb_configure_notify_event_t*>(event)->window;
    case XCB_CONFIGURE_REQUEST:
        return reinterpret_cast<xcb_configure_request_event_t*>(event)->window;
    case XCB_GRAVITY_NOTIFY:
        return reinterpret_cast<xcb_gravity_notify_event_t*>(event)->window;
    case XCB_RESIZE_REQUEST:
        return reinterpret_cast<xcb_resize_request_event_t*>(event)->window;
    case XCB_CIRCULATE_NOTIFY:
    case XCB_CIRCULATE_REQUEST:
        return reinterpret_cast<xcb_circulate_notify_event_t*>(event)->window;
    case XCB_PROPERTY_NOTIFY:
        return reinterpret_cast<xcb_property_notify_event_t*>(event)->window;
    case XCB_COLORMAP_NOTIFY:
        return reinterpret_cast<xcb_colormap_notify_event_t*>(event)->window;
    case XCB_CLIENT_MESSAGE:
        return reinterpret_cast<xcb_client_message_event_t*>(event)->window;
    default:
        // extension handling
        if (eventType == base::x11::xcb::extensions::self()->shape_notify_event()) {
            return reinterpret_cast<xcb_shape_notify_event_t*>(event)->affected_window;
        }
        if (eventType == base::x11::xcb::extensions::self()->damage_notify_event()) {
            return reinterpret_cast<xcb_damage_notify_event_t*>(event)->drawable;
        }
        return XCB_WINDOW_NONE;
    }
}

/**
 * Handles map requests of the client window
 */
template<typename Win>
bool map_request_event(Win* win, xcb_map_request_event_t* e)
{
    if (e->window != win->xcb_windows.client) {
        // Special support for the save-set feature, which is a bit broken.
        // If there's a window from one client embedded in another one,
        // e.g. using XEMBED, and the embedder suddenly loses its X connection,
        // save-set will reparent the embedded window to its closest ancestor
        // that will remains. Unfortunately, with reparenting window managers,
        // this is not the root window, but the frame (or in KWin's case,
        // it's the wrapper for the client window). In this case,
        // the wrapper will get ReparentNotify for a window it won't know,
        // which will be ignored, and then it gets MapRequest, as save-set
        // always maps. Returning true here means that Workspace::workspaceEvent()
        // will handle this MapRequest and manage this window (i.e. act as if
        // it was reparented to root window).
        if (e->parent == win->xcb_windows.wrapper) {
            return false;
        }
        // no messing with frame etc.
        return true;
    }
    // also copied in clientMessage()
    if (win->control->minimized) {
        win::set_minimized(win, false);
    }
    if (!on_current_desktop(win)) {
        if (allow_window_activation(win->space, win)) {
            activate_window(win->space, *win);
        } else {
            win::set_demands_attention(win, true);
        }
    }
    return true;
}

/**
 * Handles unmap notify events of the client window
 */
template<typename Win>
void unmap_notify_event(Win* win, xcb_unmap_notify_event_t* e)
{
    if (e->window != win->xcb_windows.client) {
        return;
    }
    if (e->event != win->xcb_windows.wrapper) {
        // most probably event from root window when initially reparenting
        bool ignore = true;
        if (e->event == win->space.base.x11_data.root_window && (e->response_type & 0x80))
            ignore = false; // XWithdrawWindow()
        if (ignore)
            return;
    }

    // check whether this is result of an XReparentWindow - client then won't be parented by wrapper
    // in this case do not release the client (causes reparent to root, removal from saveSet and
    // what not) but just destroy the client
    base::x11::xcb::tree tree(win->space.base.x11_data.connection, win->xcb_windows.client);
    xcb_window_t daddy = tree.parent();

    if (daddy == win->xcb_windows.wrapper) {
        // unmapped from a regular client state
        release_window(win, false);
    } else {
        // the client was moved to some other parent
        x11::destroy_window(win);
    }
}

template<typename Win>
void destroy_notify_event(Win* win, xcb_destroy_notify_event_t* e)
{
    if (e->window != win->xcb_windows.client) {
        return;
    }
    x11::destroy_window(win);
}

template<typename Win>
void handle_wl_surface_id_event(Win& win, xcb_client_message_event_t* e)
{
    if constexpr (requires(Win win) { win.surface_id; }) {
        if (e->type != win.space.atoms->wl_surface_id) {
            return;
        }

        win.surface_id = e->data.data32[0];
        Q_EMIT win.space.qobject->surface_id_changed(win.meta.signal_id, win.surface_id);
        Q_EMIT win.qobject->surfaceIdChanged(win.surface_id);
    }
}

/**
 * Handles client messages for the client window
 */
template<typename Win>
void client_message_event(Win* win, xcb_client_message_event_t* e)
{
    handle_wl_surface_id_event(*win, e);

    if (e->window != win->xcb_windows.client) {
        return; // ignore frame/wrapper
    }

    // WM_STATE
    if (e->type == win->space.atoms->wm_change_state) {
        if (e->data.data32[0] == XCB_ICCCM_WM_STATE_ICONIC) {
            win::set_minimized(win, true);
        }
        return;
    }
}

/**
 * Handles configure  requests of the client window
 */
template<typename Win>
void configure_request_event(Win* win, xcb_configure_request_event_t* e)
{
    if (e->window != win->xcb_windows.client) {
        return; // ignore frame/wrapper
    }
    if (win::is_resize(win) || win::is_move(win))
        return; // we have better things to do right now

    if (win->control->fullscreen || is_splash(win)) {
        // Refuse resizing of fullscreen windows and splashscreens.
        send_synthetic_configure_notify(win, frame_to_client_rect(win, win->geo.frame));
        return;
    }

    if (e->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH) {
        // first, get rid of a window border
        win->xcb_windows.client.set_border_width(0);
    }

    if (e->value_mask
        & (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_HEIGHT
           | XCB_CONFIG_WINDOW_WIDTH)) {
        configure_request(win, e->value_mask, e->x, e->y, e->width, e->height, 0, false);
    }
    if (e->value_mask & XCB_CONFIG_WINDOW_STACK_MODE) {
        restack_window(win, e->sibling, e->stack_mode, net::FromApplication, user_time(win), false);
    }

    // TODO(romangg): remove or check for size change at least?

    // Sending a synthetic configure notify always is fine, even in cases where
    // the ICCCM doesn't require this - it can be though of as 'the WM decided to move
    // the window later'. The client should not cause that many configure request,
    // so this should not have any significant impact. With user moving/resizing
    // the it should be optimized though (see also window::setGeometry()/plainResize()/move()).
    send_synthetic_configure_notify(win, frame_to_client_rect(win, win->geo.frame));

    // SELI TODO accept configure requests for isDesktop windows (because kdesktop
    // may get XRANDR resize event before kwin), but check it's still at the bottom?
}

template<typename Win>
void property_notify_event_prepare(Win& win, xcb_property_notify_event_t* event)
{
    if (event->window != win.xcb_windows.client) {
        // ignore frame/wrapper
        return;
    }

    auto& atoms = win.space.atoms;
    if (event->atom == atoms->wm_client_leader) {
        auto prop = fetch_wm_client_leader(win);
        read_wm_client_leader(win, prop);
    } else if (event->atom == atoms->kde_net_wm_shadow) {
        win::update_shadow(&win);
    } else if (event->atom == atoms->kde_skip_close_animation) {
        set_skip_close_animation(win, fetch_skip_close_animation(win).to_bool());
    }
}

/**
 * Handles property changes of the client window
 */
template<typename Win>
void property_notify_event(Win* win, xcb_property_notify_event_t* e)
{
    property_notify_event_prepare(*win, e);

    if (e->window != win->xcb_windows.client) {
        // ignore frame/wrapper
        return;
    }

    auto& atoms = win->space.atoms;
    switch (e->atom) {
    case XCB_ATOM_WM_NORMAL_HINTS:
        get_wm_normal_hints(win);
        break;
    case XCB_ATOM_WM_NAME:
        fetch_name(win);
        break;
    case XCB_ATOM_WM_ICON_NAME:
        fetch_iconic_name(win);
        break;
    case XCB_ATOM_WM_TRANSIENT_FOR: {
        auto transientFor = fetch_transient(win);
        read_transient_property(win, transientFor);
        break;
    }
    case XCB_ATOM_WM_HINTS:
        // because KWin::icon() uses WMHints as fallback
        get_icons(win);
        break;
    default:
        if (e->atom == atoms->motif_wm_hints) {
            get_motif_hints(win);
        } else if (e->atom == atoms->net_wm_sync_request_counter) {
            get_sync_counter(win);
        } else if (e->atom == atoms->kde_color_sheme) {
            update_color_scheme(win);
        } else if (e->atom == atoms->kde_screen_edge_show) {
            update_show_on_screen_edge(win);
        } else if (e->atom == atoms->kde_net_wm_appmenu_service_name) {
            check_application_menu_service_name(win);
        } else if (e->atom == atoms->kde_net_wm_appmenu_object_path) {
            check_application_menu_object_path(win);
        }
        break;
    }
}

template<typename Win>
void enter_notify_event(Win* win, xcb_enter_notify_event_t* e)
{
    if (e->event != win->frameId()) {
        // care only about entering the whole frame
        return;
    }

    auto is_mouse_driven_focus = !win->space.options->qobject->focusPolicyIsReasonable()
        || (win->space.options->qobject->focusPolicy() == focus_policy::follows_mouse
            && win->space.options->qobject->isNextFocusPrefersMouse());

    if (e->mode == XCB_NOTIFY_MODE_NORMAL
        || (e->mode == XCB_NOTIFY_MODE_UNGRAB && is_mouse_driven_focus)) {
        win::enter_event(win, QPoint(e->root_x, e->root_y));
        return;
    }
}

template<typename Win>
void leave_notify_event(Win* win, xcb_leave_notify_event_t* e)
{
    if (e->event != win->frameId()) {
        // care only about leaving the whole frame
        return;
    }
    if (e->mode == XCB_NOTIFY_MODE_NORMAL) {
        auto& mov_res = win->control->move_resize;

        if (!mov_res.button_down) {
            mov_res.contact = win::position::center;
            win::update_cursor(win);
        }
        auto lostMouse = !QRect(QPoint(), win->geo.size()).contains(QPoint(e->event_x, e->event_y));
        // 'lostMouse' wouldn't work with e.g. B2 or Keramik, which have non-rectangular decorations
        // (i.e. the LeaveNotify event comes before leaving the rect and no LeaveNotify event
        // comes after leaving the rect) - so lets check if the pointer is really outside the window

        // TODO this still sucks if a window appears above this one - it should lose the mouse
        // if this window is another client, but not if it's a popup ... maybe after KDE3.1 :(
        // (repeat after me 'AARGHL!')
        if (!lostMouse && e->detail != XCB_NOTIFY_DETAIL_INFERIOR) {
            base::x11::xcb::pointer pointer(win->space.base.x11_data.connection, win->frameId());
            if (!pointer || !pointer->same_screen || pointer->child == XCB_WINDOW_NONE) {
                // really lost the mouse
                lostMouse = true;
            }
        }
        if (lostMouse) {
            win::leave_event(win);
            if (auto deco = win::decoration(win)) {
                // sending a move instead of a leave. With leave we need to send proper coords, with
                // move it's handled internally
                QHoverEvent leaveEvent(
                    QEvent::HoverMove, QPointF(-1, -1), QPointF(-1, -1), Qt::NoModifier);
                QCoreApplication::sendEvent(deco, &leaveEvent);
            }
        }
        if (win->space.options->qobject->focusPolicy() == focus_policy::strictly_under_mouse
            && win->control->active && lostMouse) {
            win->space.stacking.delayfocus_window = {};
            reset_delay_focus_timer(win->space);
        }
        return;
    }
}

template<typename Win>
static inline bool modKeyDown(Win& win, int state)
{
    uint const keyModX = (win.space.options->qobject->keyCmdAllModKey() == Qt::Key_Meta)
        ? key_server::modXMeta()
        : key_server::modXAlt();
    return keyModX && (state & key_server::accelModMaskX()) == keyModX;
}

// return value matters only when filtering events before decoration gets them
template<typename Win>
bool button_press_event(Win* win,
                        xcb_window_t w,
                        int button,
                        int state,
                        int x,
                        int y,
                        int x_root,
                        int y_root,
                        xcb_timestamp_t time)
{
    auto con = win->space.base.x11_data.connection;
    if (win->control->move_resize.button_down) {
        if (w == win->xcb_windows.wrapper)
            xcb_allow_events(con, XCB_ALLOW_SYNC_POINTER, XCB_TIME_CURRENT_TIME);
        return true;
    }

    if (w == win->xcb_windows.wrapper || w == win->frameId() || w == win->xcb_windows.input) {
        // FRAME neco s tohohle by se melo zpracovat, nez to dostane dekorace
        update_user_time(win, time);
        const bool bModKeyHeld = modKeyDown(*win, state);

        if (win::is_splash(win) && button == XCB_BUTTON_INDEX_1 && !bModKeyHeld) {
            // hide splashwindow if the user clicks on it
            win->hideClient(true);
            if (w == win->xcb_windows.wrapper)
                xcb_allow_events(con, XCB_ALLOW_SYNC_POINTER, XCB_TIME_CURRENT_TIME);
            return true;
        }

        auto com = mouse_cmd::nothing;
        bool was_action = false;
        if (bModKeyHeld) {
            was_action = true;
            switch (button) {
            case XCB_BUTTON_INDEX_1:
                com = win->space.options->qobject->commandAll1();
                break;
            case XCB_BUTTON_INDEX_2:
                com = win->space.options->qobject->commandAll2();
                break;
            case XCB_BUTTON_INDEX_3:
                com = win->space.options->qobject->commandAll3();
                break;
            case XCB_BUTTON_INDEX_4:
            case XCB_BUTTON_INDEX_5:
                com = win->space.options->operationWindowMouseWheel(
                    button == XCB_BUTTON_INDEX_4 ? 120 : -120);
                break;
            }
        } else {
            if (w == win->xcb_windows.wrapper) {
                if (button < 4) {
                    com = win::get_mouse_command(
                        win, base::x11::xcb::to_qt_mouse_button(button), &was_action);
                } else if (button < 6) {
                    com = win::get_wheel_command(win, Qt::Vertical, &was_action);
                }
            }
        }
        if (was_action) {
            bool replay = perform_mouse_command(*win, com, {x_root, y_root});

            if (win::is_special_window(win)) {
                replay = true;
            }

            if (w == win->xcb_windows.wrapper) {
                // these can come only from a grab
                xcb_allow_events(con,
                                 replay ? XCB_ALLOW_REPLAY_POINTER : XCB_ALLOW_SYNC_POINTER,
                                 XCB_TIME_CURRENT_TIME);
            }
            return true;
        }
    }

    if (w == win->xcb_windows.wrapper) { // these can come only from a grab
        xcb_allow_events(con, XCB_ALLOW_REPLAY_POINTER, XCB_TIME_CURRENT_TIME);
        return true;
    }
    if (w == win->xcb_windows.input) {
        x = x_root - win->geo.frame.x();
        y = y_root - win->geo.frame.y();
        // New API processes core events FIRST and only passes unused ones to the decoration
        QMouseEvent ev(QMouseEvent::MouseButtonPress,
                       QPoint(x, y),
                       QPoint(x_root, y_root),
                       base::x11::xcb::to_qt_mouse_button(button),
                       base::x11::xcb::to_qt_mouse_buttons(state),
                       Qt::KeyboardModifiers());
        return win::process_decoration_button_press(win, &ev, true);
    }
    if (w == win->frameId() && win::decoration(win)) {
        if (button >= 4 && button <= 7) {
            auto const modifiers = base::x11::xcb::to_qt_keyboard_modifiers(state);
            // Logic borrowed from qapplication_x11.cpp
            const int delta = 120 * ((button == 4 || button == 6) ? 1 : -1);
            const bool hor = (((button == 4 || button == 5) && (modifiers & Qt::AltModifier))
                              || (button == 6 || button == 7));

            const QPoint angle = hor ? QPoint(delta, 0) : QPoint(0, delta);
            QWheelEvent event(QPointF(x, y),
                              QPointF(x_root, y_root),
                              QPoint(),
                              angle,
                              base::x11::xcb::to_qt_mouse_buttons(state),
                              modifiers,
                              Qt::NoScrollPhase,
                              false);
            event.setAccepted(false);
            QCoreApplication::sendEvent(win::decoration(win), &event);
            if (!event.isAccepted() && !hor) {
                if (win::titlebar_positioned_under_mouse(win)) {
                    perform_mouse_command(*win,
                                          win->space.options->operationTitlebarMouseWheel(delta),
                                          {x_root, y_root});
                }
            }
        } else {
            QMouseEvent event(QEvent::MouseButtonPress,
                              QPointF(x, y),
                              QPointF(x_root, y_root),
                              base::x11::xcb::to_qt_mouse_button(button),
                              base::x11::xcb::to_qt_mouse_buttons(state),
                              base::x11::xcb::to_qt_keyboard_modifiers(state));
            event.setAccepted(false);
            QCoreApplication::sendEvent(win::decoration(win), &event);
            if (!event.isAccepted()) {
                win::process_decoration_button_press(win, &event, false);
            }
        }
        return true;
    }
    return true;
}

// return value matters only when filtering events before decoration gets them
template<typename Win>
bool button_release_event(Win* win,
                          xcb_window_t w,
                          int button,
                          int state,
                          int x,
                          int y,
                          int x_root,
                          int y_root)
{
    auto to_qt_button = base::x11::xcb::to_qt_mouse_button;
    auto to_qt_buttons = base::x11::xcb::to_qt_mouse_buttons;

    if (w == win->frameId() && win::decoration(win)) {
        // wheel handled on buttonPress
        if (button < 4 || button > 7) {
            QMouseEvent event(QEvent::MouseButtonRelease,
                              QPointF(x, y),
                              QPointF(x_root, y_root),
                              to_qt_button(button),
                              to_qt_buttons(state) & ~to_qt_button(button),
                              base::x11::xcb::to_qt_keyboard_modifiers(state));
            event.setAccepted(false);
            QCoreApplication::sendEvent(win::decoration(win), &event);
            if (event.isAccepted() || !win::titlebar_positioned_under_mouse(win)) {
                // Click was for the deco and shall not init a doubleclick.
                win->control->deco.double_click.stop();
            }
        }
    }
    if (w == win->xcb_windows.wrapper) {
        xcb_allow_events(
            win->space.base.x11_data.connection, XCB_ALLOW_SYNC_POINTER, XCB_TIME_CURRENT_TIME);
        return true;
    }
    if (w != win->frameId() && w != win->xcb_windows.input && w != win->xcb_windows.grab) {
        return true;
    }
    if (w == win->frameId() && win->space.user_actions_menu
        && win->space.user_actions_menu->isShown()) {
        win->space.user_actions_menu->grabInput();
    }
    // translate from grab window to local coords
    x = win->geo.pos().x();
    y = win->geo.pos().y();

    // Check whether other buttons are still left pressed
    int buttonMask = XCB_BUTTON_MASK_1 | XCB_BUTTON_MASK_2 | XCB_BUTTON_MASK_3;
    if (button == XCB_BUTTON_INDEX_1)
        buttonMask &= ~XCB_BUTTON_MASK_1;
    else if (button == XCB_BUTTON_INDEX_2)
        buttonMask &= ~XCB_BUTTON_MASK_2;
    else if (button == XCB_BUTTON_INDEX_3)
        buttonMask &= ~XCB_BUTTON_MASK_3;

    if ((state & buttonMask) == 0) {
        win::end_move_resize(win);
    }
    return true;
}

// return value matters only when filtering events before decoration gets them
template<typename Win>
bool motion_notify_event(Win* win, xcb_window_t w, int state, int x, int y, int x_root, int y_root)
{
    if (w == win->frameId() && win::decoration(win) && !win->control->minimized) {
        // TODO Mouse move event dependent on state
        QHoverEvent event(QEvent::HoverMove, QPointF(x, y), QPointF(x, y));
        QCoreApplication::instance()->sendEvent(win::decoration(win), &event);
    }
    if (w != win->frameId() && w != win->xcb_windows.input && w != win->xcb_windows.grab) {
        return true; // care only about the whole frame
    }

    if (auto& mov_res = win->control->move_resize; !mov_res.button_down) {
        if (w == win->xcb_windows.input) {
            int x = x_root - win->geo.frame.x(); // + padding_left;
            int y = y_root - win->geo.frame.y(); // + padding_top;

            if (win::decoration(win)) {
                QHoverEvent event(QEvent::HoverMove, QPointF(x, y), QPointF(x, y));
                QCoreApplication::instance()->sendEvent(win::decoration(win), &event);
            }
        }
        auto newmode = modKeyDown(*win, state) ? win::position::center : win::mouse_position(win);
        if (newmode != mov_res.contact) {
            mov_res.contact = newmode;
            win::update_cursor(win);
        }
        return false;
    }
    if (w == win->xcb_windows.grab) {
        // translate from grab window to local coords
        x = win->geo.pos().x();
        y = win->geo.pos().y();
    }

    win::move_resize(win, QPoint(x, y), QPoint(x_root, y_root));
    return true;
}

template<typename Win>
void focus_in_event(Win* win, xcb_focus_in_event_t* e)
{
    using var_win = typename Win::space_t::window_t;

    if (e->event != win->xcb_windows.client) {
        return;
    }
    if (e->mode == XCB_NOTIFY_MODE_UNGRAB) {
        return;
    }
    if (e->detail == XCB_NOTIFY_DETAIL_POINTER) {
        return;
    }
    if (!win->isShown() || !on_current_desktop(win)) {
        // we unmapped it, but it got focus meanwhile ->
        // activateNextClient() already transferred focus elsewhere
        return;
    }

    for (auto win : win->space.windows) {
        std::visit(overload{[&](Win* win) { cancel_focus_out_timer(win); }, [](auto&&) {}}, win);
    }

    // check if this client is in should_get_focus list or if activation is allowed
    bool activate = allow_window_activation(win->space, win, -1U, true);

    // Remove from should_get_focus list.
    if (auto& sgf = win->space.stacking.should_get_focus; contains(sgf, var_win(win))) {
        // Remove also all sooner elements that should have got FocusIn, but didn't for some reason
        // (and also won't anymore, because they were sooner).
        while (sgf.front() != var_win(win)) {
            sgf.pop_front();
        }

        // Finally remove 'win'.
        sgf.pop_front();
    }

    if (activate) {
        win::set_active(win, true);
    } else {
        // this updateXTime() is necessary - as FocusIn events don't have
        // a timestamp *sigh*, kwin's timestamp would be older than the timestamp
        // that was used by whoever caused the focus change, and therefore
        // the attempt to restore the focus would fail due to old timestamp
        base::x11::update_time_from_clock(win->space.base);

        if (auto& sgf = win->space.stacking.should_get_focus; !sgf.empty()) {
            std::visit(overload{[](auto&& fc) { request_focus(fc->space, *fc); }}, sgf.back());
        } else if (auto last = win->space.stacking.last_active) {
            std::visit(overload{[](auto&& last) { request_focus(last->space, *last); }}, *last);
        }

        win::set_demands_attention(win, true);
    }
}

template<typename Win>
void focus_out_event(Win* win, xcb_focus_out_event_t* e)
{
    if (e->event != win->xcb_windows.client) {
        return; // only window gets focus
    }
    if (e->mode == XCB_NOTIFY_MODE_GRAB)
        return; // we don't care
    if (e->detail != XCB_NOTIFY_DETAIL_NONLINEAR
        && e->detail != XCB_NOTIFY_DETAIL_NONLINEAR_VIRTUAL)
        // SELI check all this
        return; // hack for motif apps like netscape
    if (QApplication::activePopupWidget())
        return;

    // When a client loses focus, FocusOut events are usually immediatelly
    // followed by FocusIn events for another client that gains the focus
    // (unless the focus goes to another screen, or to the nofocus widget).
    // Without this check, the former focused client would have to be
    // deactivated, and after that, the new one would be activated, with
    // a short time when there would be no active client. This can cause
    // flicker sometimes, e.g. when a fullscreen is shown, and focus is transferred
    // from it to its transient, the fullscreen would be kept in the Active layer
    // at the beginning and at the end, but not in the middle, when the active
    // client would be temporarily none (see belong_to_layer() ).
    // Therefore the setActive(false) call is moved to the end of the current
    // event queue. If there is a matching FocusIn event in the current queue
    // this will be processed before the setActive(false) call and the activation
    // of the Client which gained FocusIn will automatically deactivate the
    // previously active client.
    if (!win->focus_out_timer) {
        win->focus_out_timer = new QTimer(win->qobject.get());
        win->focus_out_timer->setSingleShot(true);
        win->focus_out_timer->setInterval(0);
        QObject::connect(win->focus_out_timer, &QTimer::timeout, win->qobject.get(), [win]() {
            win::set_active(win, false);
        });
    }
    win->focus_out_timer->start();
}

// performs _NET_WM_MOVERESIZE
template<typename Win>
void net_move_resize(Win* win, int x_root, int y_root, net::Direction direction)
{
    auto& mov_res = win->control->move_resize;
    auto& cursor = win->space.input->cursor;

    if (direction == net::Move) {
        // move cursor to the provided position to prevent the window jumping there on first
        // movement the expectation is that the cursor is already at the provided position, thus
        // it's more a safety measurement
        cursor->set_pos(QPoint(x_root, y_root));
        perform_mouse_command(*win, mouse_cmd::move, {x_root, y_root});
    } else if (mov_res.enabled && direction == net::MoveResizeCancel) {
        win::finish_move_resize(win, true);
        mov_res.button_down = false;
        win::update_cursor(win);
    } else if (direction >= net::TopLeft && direction <= net::Left) {
        static const win::position convert[] = {win::position::top_left,
                                                win::position::top,
                                                win::position::top_right,
                                                win::position::right,
                                                win::position::bottom_right,
                                                win::position::bottom,
                                                win::position::bottom_left,
                                                win::position::left};
        if (!win->isResizable())
            return;
        if (mov_res.enabled) {
            win::finish_move_resize(win, false);
        }
        mov_res.button_down = true;

        // map from global
        mov_res.offset = QPoint(x_root - win->geo.pos().x(), y_root - win->geo.pos().y());
        mov_res.inverted_offset
            = QPoint(win->geo.size().width(), win->geo.size().height()) - mov_res.offset;
        mov_res.unrestricted = false;
        mov_res.contact = convert[direction];
        if (!win::start_move_resize(win)) {
            mov_res.button_down = false;
        }
        win::update_cursor(win);
    } else if (direction == net::KeyboardMove) {
        // ignore mouse coordinates given in the message, mouse position is used by the moving
        // algorithm
        cursor->set_pos(win->geo.frame.center());
        perform_mouse_command(*win, mouse_cmd::unrestricted_move, win->geo.frame.center());
    } else if (direction == net::KeyboardSize) {
        // ignore mouse coordinates given in the message, mouse position is used by the resizing
        // algorithm
        cursor->set_pos(win->geo.frame.bottomRight());
        perform_mouse_command(*win, mouse_cmd::unrestricted_resize, win->geo.frame.bottomRight());
    }
}

template<typename Win>
void key_press_event(Win* win, uint key_code, xcb_timestamp_t time)
{
    update_user_time(win, time);
    win::key_press_event(win, key_code);
}

/**
 * General handler for XEvents concerning the client window
 */
template<typename Win>
bool window_event(Win* win, xcb_generic_event_t* e)
{
    if (find_event_window(e) == win->xcb_windows.client) {
        // avoid doing stuff on frame or wrapper
        net::Properties dirtyProperties;
        net::Properties2 dirtyProperties2;
        auto old_opacity = win->opacity();

        // pass through the NET stuff
        win->net_info->event(e, &dirtyProperties, &dirtyProperties2);

        if ((dirtyProperties & net::WMName) != 0) {
            fetch_name(win);
        }
        if ((dirtyProperties & net::WMIconName) != 0) {
            fetch_iconic_name(win);
        }
        if ((dirtyProperties & net::WMStrut) != 0
            || (dirtyProperties2 & net::WM2ExtendedStrut) != 0) {
            update_space_areas(win->space);
        }
        if ((dirtyProperties & net::WMIcon) != 0) {
            get_icons(win);
        }

        // Note there's a difference between userTime() and net_info->userTime()
        // net_info->userTime() is the value of the property, userTime() also includes
        // updates of the time done by KWin (ButtonPress on windowrapper etc.).
        if ((dirtyProperties2 & net::WM2UserTime) != 0) {
            mark_as_user_interaction(win->space);
            update_user_time(win, win->net_info->userTime());
        }
        if (dirtyProperties2 & net::WM2Opacity) {
            if (win->space.base.render->compositor->scene) {
                add_full_repaint(*win);
                Q_EMIT win->qobject->opacityChanged(old_opacity);
            } else {
                // forward to the frame if there's possibly another compositing manager running
                net::win_info i(win->space.base.x11_data.connection,
                                win->frameId(),
                                win->space.base.x11_data.root_window,
                                net::Properties(),
                                net::Properties2());
                i.setOpacity(win->net_info->opacity());
            }
        }
        if (dirtyProperties2 & net::WM2FrameOverlap) {
            // Property is deprecated.
        }
        if (dirtyProperties2.testFlag(net::WM2WindowRole)) {
            Q_EMIT win->qobject->windowRoleChanged();
        }
        if (dirtyProperties2.testFlag(net::WM2WindowClass)) {
            fetch_wm_class(*win);
        }
        if (dirtyProperties2.testFlag(net::WM2BlockCompositing)) {
            win->setBlockingCompositing(win->net_info->isBlockingCompositing());
        }
        if (dirtyProperties2.testFlag(net::WM2GroupLeader)) {
            check_group(win, nullptr);

            // Group affects isMinimizable()
            update_allowed_actions(win);
        }
        if (dirtyProperties2.testFlag(net::WM2Urgency)) {
            update_urgency(win);
        }
        if (dirtyProperties2 & net::WM2OpaqueRegion) {
            fetch_wm_opaque_region(*win);
        }
        if (dirtyProperties2 & net::WM2DesktopFileName) {
            win::set_desktop_file_name(win, QByteArray(win->net_info->desktopFileName()));
        }
        if (dirtyProperties2 & net::WM2GTKFrameExtents) {
            auto& orig_extents = win->geo.update.original.client_frame_extents;

            orig_extents = win->geo.client_frame_extents;
            win->geo.client_frame_extents = gtk_frame_extents(win);

            // Only do a size update when there is a change and no other geometry update is
            // pending at the moment, which would update it later on anyway.
            if (orig_extents != win->geo.client_frame_extents
                && win->geo.update.pending == pending_geometry::none) {
                // The frame geometry stays the same so we just update our server geometry and use
                // the latest synced frame geometry.
                update_server_geometry(win, win->synced_geometry.frame);
                discard_buffer(*win);
            }
        }
    }

    const uint8_t eventType = e->response_type & ~0x80;
    switch (eventType) {
    case XCB_UNMAP_NOTIFY:
        unmap_notify_event(win, reinterpret_cast<xcb_unmap_notify_event_t*>(e));
        break;
    case XCB_DESTROY_NOTIFY:
        destroy_notify_event(win, reinterpret_cast<xcb_destroy_notify_event_t*>(e));
        break;
    case XCB_MAP_REQUEST:
        // this one may pass the event to workspace
        return map_request_event(win, reinterpret_cast<xcb_map_request_event_t*>(e));
    case XCB_CONFIGURE_REQUEST:
        configure_request_event(win, reinterpret_cast<xcb_configure_request_event_t*>(e));
        break;
    case XCB_PROPERTY_NOTIFY:
        property_notify_event(win, reinterpret_cast<xcb_property_notify_event_t*>(e));
        break;
    case XCB_KEY_PRESS:
        update_user_time(win, reinterpret_cast<xcb_key_press_event_t*>(e)->time);
        break;
    case XCB_BUTTON_PRESS: {
        const auto* event = reinterpret_cast<xcb_button_press_event_t*>(e);
        update_user_time(win, event->time);
        button_press_event(win,
                           event->event,
                           event->detail,
                           event->state,
                           event->event_x,
                           event->event_y,
                           event->root_x,
                           event->root_y,
                           event->time);
        break;
    }
    case XCB_KEY_RELEASE:
        // don't update user time on releases
        // e.g. if the user presses Alt+F2, the Alt release
        // would appear as user input to the currently active window
        break;
    case XCB_BUTTON_RELEASE: {
        const auto* event = reinterpret_cast<xcb_button_release_event_t*>(e);
        // don't update user time on releases
        // e.g. if the user presses Alt+F2, the Alt release
        // would appear as user input to the currently active window
        button_release_event(win,
                             event->event,
                             event->detail,
                             event->state,
                             event->event_x,
                             event->event_y,
                             event->root_x,
                             event->root_y);
        break;
    }
    case XCB_MOTION_NOTIFY: {
        const auto* event = reinterpret_cast<xcb_motion_notify_event_t*>(e);
        motion_notify_event(win,
                            event->event,
                            event->state,
                            event->event_x,
                            event->event_y,
                            event->root_x,
                            event->root_y);
        win->space.focusMousePos = QPoint(event->root_x, event->root_y);
        break;
    }
    case XCB_ENTER_NOTIFY: {
        auto* event = reinterpret_cast<xcb_enter_notify_event_t*>(e);
        enter_notify_event(win, event);
        // MotionNotify is guaranteed to be generated only if the mouse
        // move start and ends in the window; for cases when it only
        // starts or only ends there, Enter/LeaveNotify are generated.
        // Fake a MotionEvent in such cases to make handle of mouse
        // events simpler (Qt does that too).
        motion_notify_event(win,
                            event->event,
                            event->state,
                            event->event_x,
                            event->event_y,
                            event->root_x,
                            event->root_y);
        win->space.focusMousePos = QPoint(event->root_x, event->root_y);
        break;
    }
    case XCB_LEAVE_NOTIFY: {
        auto* event = reinterpret_cast<xcb_leave_notify_event_t*>(e);
        motion_notify_event(win,
                            event->event,
                            event->state,
                            event->event_x,
                            event->event_y,
                            event->root_x,
                            event->root_y);
        leave_notify_event(win, event);
        break;
    }
    case XCB_FOCUS_IN:
        focus_in_event(win, reinterpret_cast<xcb_focus_in_event_t*>(e));
        break;
    case XCB_FOCUS_OUT:
        focus_out_event(win, reinterpret_cast<xcb_focus_out_event_t*>(e));
        break;
    case XCB_REPARENT_NOTIFY:
        break;
    case XCB_CLIENT_MESSAGE:
        client_message_event(win, reinterpret_cast<xcb_client_message_event_t*>(e));
        break;
    case XCB_EXPOSE: {
        auto event = reinterpret_cast<xcb_expose_event_t*>(e);
        if (event->window == win->frameId()
            && win->space.base.render->compositor->state != render::state::on) {
            // TODO: only repaint required areas
            win::trigger_decoration_repaint(win);
        }
        break;
    }
    default:
        if (eventType == base::x11::xcb::extensions::self()->shape_notify_event()
            && reinterpret_cast<xcb_shape_notify_event_t*>(e)->affected_window
                == win->xcb_windows.client) {
            // workaround for #19644
            detect_shape(*win);
            update_shape(win);
        }
        if (eventType == base::x11::xcb::extensions::self()->damage_notify_event()
            && reinterpret_cast<xcb_damage_notify_event_t*>(e)->drawable == win->frameId()) {
            damage_handle_notify_event(*win);
        }
        break;
    }
    return true; // eat all events
}

}
