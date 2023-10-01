/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "actions.h"
#include "activation.h"
#include "appmenu.h"
#include "client_machine.h"
#include "deco.h"
#include "focus_stealing.h"
#include "meta.h"
#include "placement.h"
#include "session.h"
#include "user_time.h"
#include "win_info.h"
#include "window_create.h"
#include "xcb.h"

#include "base/logging.h"
#include "win/input.h"
#include "win/layers.h"
#include "win/options.h"
#include "win/rules/find.h"
#include "win/session.h"
#include <win/tabbox/tabbox_client_impl.h>

namespace KWin::win::x11
{

template<typename Win>
void embed_client(Win* win, xcb_visualid_t visualid, xcb_colormap_t colormap, uint8_t depth)
{
    assert(win->frameId() == XCB_WINDOW_NONE);
    assert(win->xcb_windows.wrapper == XCB_WINDOW_NONE);

    uint32_t const zero_value = 0;
    auto conn = win->space.base.x11_data.connection;

    // We don't want the window to be destroyed when we quit
    xcb_change_save_set(conn, XCB_SET_MODE_INSERT, win->xcb_windows.client);

    win->xcb_windows.client.select_input(zero_value);
    win->xcb_windows.client.unmap();
    win->xcb_windows.client.set_border_width(zero_value);

    // Note: These values must match the order in the xcb_cw_t enum
    uint32_t const cw_values[] = {
        0,        // back_pixmap
        0,        // border_pixel
        colormap, // colormap
        win->space.input->cursor->x11_cursor(Qt::ArrowCursor),
    };

    auto const cw_mask = XCB_CW_BACK_PIXMAP | XCB_CW_BORDER_PIXEL | XCB_CW_COLORMAP | XCB_CW_CURSOR;

    auto const common_event_mask = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE
        | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_BUTTON_PRESS
        | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION
        | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_KEYMAP_STATE | XCB_EVENT_MASK_FOCUS_CHANGE
        | XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
        | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;

    auto const frame_event_mask
        = common_event_mask | XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_VISIBILITY_CHANGE;
    auto const wrapper_event_mask = common_event_mask | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;

    auto const client_event_mask = XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_PROPERTY_CHANGE
        | XCB_EVENT_MASK_COLOR_MAP_CHANGE | XCB_EVENT_MASK_ENTER_WINDOW
        | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE;

    // Create the frame window
    auto frame = xcb_generate_id(conn);
    xcb_create_window(conn,
                      depth,
                      frame,
                      win->space.base.x11_data.root_window,
                      0,
                      0,
                      1,
                      1,
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      visualid,
                      cw_mask,
                      cw_values);
    win->xcb_windows.outer.reset(win->space.base.x11_data.connection, frame);

    // Create the wrapper window
    auto wrapperId = xcb_generate_id(conn);
    xcb_create_window(conn,
                      depth,
                      wrapperId,
                      frame,
                      0,
                      0,
                      1,
                      1,
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      visualid,
                      cw_mask,
                      cw_values);

    win->xcb_windows.wrapper.reset(win->space.base.x11_data.connection, wrapperId);
    win->xcb_windows.client.reparent(win->xcb_windows.wrapper);

    // We could specify the event masks when we create the windows, but the original
    // Xlib code didn't.  Let's preserve that behavior here for now so we don't end up
    // receiving any unexpected events from the wrapper creation or the reparenting.
    win->xcb_windows.outer.select_input(frame_event_mask);
    win->xcb_windows.wrapper.select_input(wrapper_event_mask);
    win->xcb_windows.client.select_input(client_event_mask);

    win->control->update_mouse_grab();
}

template<typename Win>
void prepare_decoration(Win* win)
{
    auto colorSchemeCookie = fetch_color_scheme(win);
    auto applicationMenuServiceNameCookie = fetch_application_menu_service_name(win);
    auto applicationMenuObjectPathCookie = fetch_application_menu_object_path(win);

    read_color_scheme(win, colorSchemeCookie);

    read_application_menu_service_name(win, applicationMenuServiceNameCookie);
    read_application_menu_object_path(win, applicationMenuObjectPathCookie);

    // Also gravitates
    win->updateDecoration(false);
}

template<typename Win>
bool created_window_may_activate(Win& win, Win& act_win)
{
    if (enum_index(
            win.control->rules.checkFSP(win.space.options->qobject->focusStealingPreventionLevel()))
        <= 0) {
        // Always allowed if focus stealing prevention is turned off.
        return true;
    }

    if (belong_to_same_application(&act_win, &win, same_client_check::relaxed_for_active)) {
        // New windows of the application currently with an active window may activate.
        return true;
    }

    auto sameApplicationActiveHackPredicate = [&win](Win const* other) {
        // Ignore already existing splashes, toolbars, utilities and menus, as the app may show
        // those before the main window.
        return !is_splash(other) && !is_toolbar(other) && !is_utility(other) && !is_menu(other)
            && other != &win
            && belong_to_same_application(other, &win, same_client_check::relaxed_for_active);
    };

    if (win.transient->lead()) {
        auto get_casted_leads = [&win]() {
            std::vector<Win*> ret;
            const auto mcs = win.transient->leads();
            for (auto mc : mcs) {
                ret.push_back(mc);
            }
            return ret;
        };

        if (win.transient->is_follower_of(&act_win)) {
            // Is transient for currently active window, even though it's not the same app (e.g.
            // kcookiejar dialog).
            return true;
        }
        if (win.groupTransient()
            && !find_in_list<Win, Win>(get_casted_leads(), sameApplicationActiveHackPredicate)) {
            // Standalone transient
            return true;
        }

        // New child window may not activate.
        return false;
    }

    for (auto other : win.space.windows) {
        if (std::visit(overload{[&](Win* other) {
                                    return other->control
                                        && sameApplicationActiveHackPredicate(other);
                                },
                                [](auto&&) { return false; }},
                       other)) {
            return false;
        }
    }

    return true;
}

template<typename Win>
xcb_timestamp_t query_timestamp(Win& win)
{
    // If it's the first window for its application (i.e. there's no other window from the same
    // app), use the _KDE_NET_WM_USER_CREATION_TIME trick. Otherwise, refuse activation of a
    // window from already running application if this application is not the active one (unless
    // focus stealing prevention is turned off).
    if (auto act = most_recently_activated_window(win.space)) {
        if (!std::visit(overload{[&](Win* act) { return created_window_may_activate(win, *act); },
                                 [](auto&&) { return true; }},
                        *act)) {
            return 0;
        }
    }

    return read_user_creation_time(win);
}

template<typename Win>
xcb_timestamp_t read_user_time_map_timestamp(Win* win)
{
    xcb_timestamp_t time = win->net_info->userTime();
    if (time != -1U) {
        return time;
    }

    return query_timestamp(*win);
}

template<typename Win>
bool init_controlled_window_from_session(Win& win, bool isMapped)
{
    auto session = take_session_info(win.space, &win);
    if (!session) {
        return false;
    }

    auto init_minimize = session->minimized;
    win.user_no_border = session->noBorder;

    set_shortcut(&win, win.control->rules.checkShortcut(session->shortcut, true));

    init_minimize = win.control->rules.checkMinimize(init_minimize, !isMapped);
    win.user_no_border = win.control->rules.checkNoBorder(win.user_no_border, !isMapped);

    // We setup compositing already here so a desktop presence change can access effects.
    win.setupCompositing();

    // Initial desktop placement
    using desks = QVector<virtual_desktop*>;
    std::optional<desks> initial_desktops;

    if (session->onAllDesktops) {
        initial_desktops = desks{};
    } else if (auto desktop
               = win.space.virtual_desktop_manager->desktopForX11Id(session->desktop)) {
        initial_desktops = desks{desktop};
    }

    if (!initial_desktops.has_value()) {
        initial_desktops = is_desktop(&win)
            ? desks{}
            : desks{win.space.virtual_desktop_manager->currentDesktop()};
    }

    set_desktops(win,
                 win.control->rules.checkDesktops(
                     *win.space.virtual_desktop_manager, *initial_desktops, !isMapped));
    win.net_info->setDesktop(get_desktop(win));

    propagate_on_all_desktops_to_children(win);

    win.geo.client_frame_extents = gtk_frame_extents(&win);
    win.geo.update.original.client_frame_extents = win.geo.client_frame_extents;

    prepare_decoration(&win);

    // Set size before placement.
    auto frame_geo = session->geometry;

    win.geo.frame = frame_geo;

    if (isMapped) {
        // TODO(romangg): Or should we just in general assume a window with session info is
        //                unmapped and remove the additional argument to this function?
        qCWarning(KWIN_CORE)
            << "Unexpected client behavior: session info provided for already mapped client.";
    }
    auto const placement_area = place_session(&win, frame_geo);

    // CT: Extra check for stupid jdk 1.3.1. But should make sense in general
    // if client has initial state set to Iconic and is transient with a parent
    // window that is not Iconic, set init_state to Normal
    if (init_minimize) {
        auto leads = win.transient->leads();
        for (auto lead : leads) {
            if (lead->isShown()) {
                // SELI TODO: Even e.g. for net::Utility?
                init_minimize = false;
            }
        }
    }

    // If a dialog is shown for minimized window, minimize it too
    if (!init_minimize && win.transient->lead()
        && win.space.session_manager->state() != session_state::saving) {
        bool visible_parent = false;

        for (auto const& lead : win.transient->leads()) {
            if (lead->isShown()) {
                visible_parent = true;
            }
        }

        if (!visible_parent) {
            init_minimize = true;
            set_demands_attention(&win, true);
        }
    }

    if (init_minimize) {
        set_minimized(&win, true, true);
    }

    // Other settings from the previous session

    // Session restored windows are not considered to be new windows WRT rules,
    // I.e. obey only forcing rules
    set_keep_above(&win, session->keepAbove);
    set_keep_below(&win, session->keepBelow);
    set_original_skip_taskbar(&win, session->skipTaskbar);
    set_skip_pager(&win, session->skipPager);
    set_skip_switcher(&win, session->skipSwitcher);
    win.setOpacity(session->opacity);

    if (static_cast<maximize_mode>(session->maximized) != maximize_mode::restore) {
        maximize(&win, static_cast<maximize_mode>(session->maximized));
        win.geo.restore.max = session->restore;
    }
    if (session->fullscreen) {
        win.setFullScreen(true, false);
        win.geo.restore.max = session->fsrestore;
    }

    check_offscreen_position(win.geo.restore.max, placement_area);

    update_allowed_actions(&win, true);

    // Set initial user time directly
    //
    // Falling back to creation time (through call to read_user_creation_time) would just mess
    // things up during session startup, as possibly many apps are started up at the same time. If
    // there's no active window yet, no timestamp will be needed, as plain allow_window_activation()
    // will return true in such case. And if there's already active window, it's better not to
    // activate the new one. Unless it was the active window at the time of session saving and there
    // was no user interaction yet, this check will be done in manage().
    win.user_time = win.net_info->userTime();

    // And do what Win::updateUserTime() does
    win.group->updateUserTime(win.user_time);

    // This should avoid flicker, because real restacking is done
    // only after manage() finishes because of blocking, but the window is shown sooner
    win.xcb_windows.outer.lower();

    if (session->stackingOrder != -1) {
        win.sm_stacking_order = session->stackingOrder;
        restore_session_stacking_order(&win.space, &win);
    }

    if (!win.space.base.render->compositor->scene) {
        // set to true in case compositing is turned on later. bug #160393
        win.render_data.ready_for_painting = true;
    }

    if (win.isShown()) {
        auto allow{false};
        if (session->active) {
            allow = !win.space.was_user_interaction || !win.space.stacking.active
                || (win.space.stacking.active
                    && std::visit(overload{[&](auto&& win) { return is_desktop(win); }},
                                  *win.space.stacking.active));
        }

        if (on_current_desktop(win) && !isMapped && !allow && session->stackingOrder < 0) {
            restack_client_under_active(win.space, win);
        }

        update_visibility(&win);

        if (!isMapped) {
            if (allow && on_current_desktop(win)) {
                if (!is_special_window(&win)) {
                    if (win.space.options->qobject->focusPolicyIsReasonable()
                        && wants_tab_focus(&win)) {
                        request_focus(win.space, win);
                    }
                }
            }
        }
    } else {
        update_visibility(&win);
    }

    return true;
}

/**
 * Manages the clients. This means handling the very first maprequest:
 * reparenting, initial geometry, initial state, placement, etc.
 * Returns false if KWin is not going to manage this window.
 */
template<typename Space>
auto create_controlled_window(xcb_window_t xcb_win, bool isMapped, Space& space) ->
    typename Space::x11_window*
{
    using Win = typename Space::x11_window;

    blocker block(space.stacking.order);

    base::x11::xcb::window_attributes attr(space.base.x11_data.connection, xcb_win);
    base::x11::xcb::geometry windowGeometry(space.base.x11_data.connection, xcb_win);
    if (attr.is_null() || windowGeometry.is_null()) {
        return nullptr;
    }

    auto win = new Win(xcb_win, space);

    // So that decorations don't start with size being (0,0).
    win->geo.frame = QRect(0, 0, 100, 100);

    setup_space_window_connections(&space, win);

    using compositor_t = typename Space::base_t::render_t::compositor_t;
    if constexpr (requires(compositor_t comp) { comp.update_blocking(win); }) {
        auto comp = space.base.render->compositor.get();
        QObject::connect(
            win->qobject.get(),
            &Win::qobject_t::blockingCompositingChanged,
            comp->qobject.get(),
            [comp, win](auto blocks) { comp->update_blocking(blocks ? win : nullptr); });
    }

    QObject::connect(win->qobject.get(),
                     &Win::qobject_t::fullScreenChanged,
                     space.edges->qobject.get(),
                     &screen_edger_qobject::checkBlocking);

    // From this place on, manage() must not return false
    win->control = std::make_unique<typename Win::control_t>(win);

    win->supported_default_types = supported_managed_window_types_mask;
    win->geo.has_in_content_deco = true;

    win->sync_request.timestamp = space.base.x11_data.time;

    setup_window_control_connections(win);
    win->control->tabbox = std::make_unique<win::tabbox_client_impl<typename Space::window_t>>(win);
    win->control->setup_color_scheme();

    QObject::connect(win->client_machine,
                     &client_machine::localhostChanged,
                     win->qobject.get(),
                     [win] { win->updateCaption(); });
    QObject::connect(space.options->qobject.get(),
                     &win::options_qobject::configChanged,
                     win->qobject.get(),
                     [win] { win->control->update_mouse_grab(); });
    QObject::connect(space.options->qobject.get(),
                     &win::options_qobject::condensedTitleChanged,
                     win->qobject.get(),
                     [win] { win->updateCaption(); });

    QObject::connect(win->qobject.get(),
                     &window_qobject::moveResizeCursorChanged,
                     win->qobject.get(),
                     [win](auto cursor) {
                         auto nativeCursor = win->space.input->cursor->x11_cursor(cursor);
                         win->xcb_windows.outer.define_cursor(nativeCursor);
                         if (win->xcb_windows.input.is_valid()) {
                             win->xcb_windows.input.define_cursor(nativeCursor);
                         }
                         if (win->control->move_resize.enabled) {
                             // changing window attributes doesn't change cursor if there's
                             // pointer grab active
                             xcb_change_active_pointer_grab(
                                 win->space.base.x11_data.connection,
                                 nativeCursor,
                                 win->space.base.x11_data.time,
                                 XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
                                     | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_ENTER_WINDOW
                                     | XCB_EVENT_MASK_LEAVE_WINDOW);
                         }
                     });

    block_geometry_updates(win, true);

    embed_client(win, attr->visual, attr->colormap, windowGeometry->depth);

    win->xcb_visual = attr->visual;
    win->render_data.bit_depth = windowGeometry->depth;

    const net::Properties properties = net::WMDesktop | net::WMState | net::WMWindowType
        | net::WMStrut | net::WMName | net::WMIconGeometry | net::WMIcon | net::WMPid
        | net::WMIconName;
    const net::Properties2 properties2 = net::WM2BlockCompositing | net::WM2WindowClass
        | net::WM2WindowRole | net::WM2UserTime | net::WM2ExtendedStrut | net::WM2Opacity
        | net::WM2FullscreenMonitors | net::WM2GroupLeader | net::WM2Urgency | net::WM2Input
        | net::WM2Protocols | net::WM2InitialMappingState | net::WM2IconPixmap
        | net::WM2OpaqueRegion | net::WM2DesktopFileName | net::WM2GTKFrameExtents
        | net::WM2GTKApplicationId;

    auto wmClientLeaderCookie = fetch_wm_client_leader(*win);
    auto skipCloseAnimationCookie = fetch_skip_close_animation(*win);
    auto showOnScreenEdgeCookie = fetch_show_on_screen_edge(win);
    auto transientCookie = fetch_transient(win);

    win->geometry_hints.init(win->xcb_windows.client);
    win->motif_hints.init(win->xcb_windows.client);

    win->net_info = new win_info<Win>(win,
                                      win->xcb_windows.client,
                                      win->space.base.x11_data.root_window,
                                      properties,
                                      properties2);

    if (is_desktop(win) && win->render_data.bit_depth == 32) {
        // force desktop windows to be opaque. It's a desktop after all, there is no window
        // below
        win->render_data.bit_depth = 24;
    }
    win->colormap = attr->colormap;

    fetch_wm_class(*win);
    read_wm_client_leader(*win, wmClientLeaderCookie);
    fetch_wm_client_machine(*win);
    get_sync_counter(win);

    // First only read the caption text, so that win::setup_rules(..) can use it for matching,
    // and only then really set the caption using setCaption(), which checks for duplicates etc.
    // and also relies on rules already existing
    win->meta.caption.normal = read_name(win);

    rules::setup_rules(win);
    set_caption(win, win->meta.caption.normal, true);

    QObject::connect(win->qobject.get(),
                     &Win::qobject_t::windowClassChanged,
                     win->qobject.get(),
                     [win] { rules::evaluate_rules(win); });

    if (base::x11::xcb::extensions::self()->is_shape_available()) {
        xcb_shape_select_input(space.base.x11_data.connection, win->xcb_windows.client, true);
    }

    detect_shape(*win);
    detect_no_border(win);
    fetch_iconic_name(win);

    check_group(win, nullptr);
    update_urgency(win);

    update_allowed_actions(win);

    win->transient->set_modal((win->net_info->state() & net::Modal) != 0);
    read_transient_property(win, transientCookie);

    QByteArray desktopFileName{win->net_info->desktopFileName()};
    if (desktopFileName.isEmpty()) {
        desktopFileName = win->net_info->gtkApplicationId();
    }
    set_desktop_file_name(win,
                          win->control->rules.checkDesktopFile(desktopFileName, true).toUtf8());
    get_icons(win);
    QObject::connect(win->qobject.get(),
                     &window_qobject::desktopFileNameChanged,
                     win->qobject.get(),
                     [win] { get_icons(win); });

    win->geometry_hints.read();
    get_motif_hints(win, true);
    fetch_wm_opaque_region(*win);
    set_skip_close_animation(*win, skipCloseAnimationCookie.to_bool());

    // TODO: Try to obey all state information from net_info->state()

    set_original_skip_taskbar(win, (win->net_info->state() & net::SkipTaskbar) != 0);
    set_skip_pager(win, (win->net_info->state() & net::SkipPager) != 0);
    set_skip_switcher(win, (win->net_info->state() & net::SkipSwitcher) != 0);

    auto init_minimize = !isMapped && (win->net_info->initialMappingState() == net::Iconic);
    if (win->net_info->state() & net::Hidden) {
        init_minimize = true;
    }

    // Make sure that the input window is created before we update the stacking order
    // TODO(romangg): Does it matter that the frame geometry is not set yet here?
    update_input_window(win, win->geo.frame);

    update_layer(win);

    if (!init_controlled_window_from_session(*win, isMapped)) {
        set_shortcut(win, win->control->rules.checkShortcut(QString(), true));

        init_minimize = win->control->rules.checkMinimize(init_minimize, !isMapped);
        win->user_no_border = win->control->rules.checkNoBorder(win->user_no_border, !isMapped);

        // We setup compositing already here so a desktop presence change can access effects.
        win->setupCompositing();

        // Initial desktop placement
        using desks = QVector<virtual_desktop*>;
        std::optional<desks> initial_desktops;

        // If this window is transient, ensure that it is opened on the
        // same window as its parent.  this is necessary when an application
        // starts up on a different desktop than is currently displayed.
        if (win->transient->lead()) {
            auto leads = win->transient->leads();
            bool on_current = false;
            bool on_all = false;
            Win* maincl = nullptr;

            // This is slightly duplicated from win::place_on_main_window()
            for (auto const& lead : leads) {
                if (leads.size() > 1 && is_special_window(lead)
                    && !(win->net_info->state() & net::Modal)) {
                    // Don't consider group-transients and toolbars etc when placing
                    // except when it's modal (blocks specials as well).
                    continue;
                }

                maincl = lead;
                if (on_current_desktop(*lead)) {
                    on_current = true;
                }
                if (on_all_desktops(*lead)) {
                    on_all = true;
                }
            }

            if (on_all) {
                initial_desktops = desks{};
            } else if (on_current) {
                initial_desktops = desks{space.virtual_desktop_manager->currentDesktop()};
            } else if (maincl != nullptr) {
                initial_desktops = maincl->topo.desktops;
            }
        } else {
            // A transient shall appear on its leader and not drag that around.
            auto desktop_id = 0;
            if (win->net_info->desktop()) {
                // Window had the initial desktop property, force it
                desktop_id = win->net_info->desktop();
            }
            if (desktop_id) {
                if (desktop_id == net::OnAllDesktops) {
                    initial_desktops = desks{};
                } else if (auto desktop
                           = space.virtual_desktop_manager->desktopForX11Id(desktop_id)) {
                    initial_desktops = desks{desktop};
                }
            }
        }

        if (!initial_desktops.has_value()) {
            initial_desktops = is_desktop(win)
                ? desks{}
                : desks{space.virtual_desktop_manager->currentDesktop()};
        }

        set_desktops(*win,
                     win->control->rules.checkDesktops(
                         *space.virtual_desktop_manager, *initial_desktops, !isMapped));
        win->net_info->setDesktop(get_desktop(*win));

        propagate_on_all_desktops_to_children(*win);

        win->geo.client_frame_extents = gtk_frame_extents(win);
        win->geo.update.original.client_frame_extents = win->geo.client_frame_extents;

        prepare_decoration(win);

        // Set size before placement.
        QRect frame_geo;

        auto const client_geo = windowGeometry.rect();

        if (isMapped) {
            win->synced_geometry.client = client_geo;
        }

        auto const frame_pos = client_geo.topLeft() - QPoint(left_border(win), top_border(win))
            + QPoint(win->geo.client_frame_extents.left(), win->geo.client_frame_extents.top());
        auto const frame_size = size_for_client_size(win, client_geo.size(), size_mode::any, false);
        frame_geo = QRect(frame_pos, frame_size);

        win->geo.frame = frame_geo;

        if (isMapped) {
            place_mapped(win, frame_geo);
        } else {
            place_unmapped(win, frame_geo);
        }

        // CT: Extra check for stupid jdk 1.3.1. But should make sense in general
        // if client has initial state set to Iconic and is transient with a parent
        // window that is not Iconic, set init_state to Normal
        if (init_minimize) {
            auto leads = win->transient->leads();
            for (auto lead : leads) {
                if (lead->isShown()) {
                    // SELI TODO: Even e.g. for net::Utility?
                    init_minimize = false;
                }
            }
        }

        auto const isSessionSaving = space.session_manager->state() == session_state::saving;

        // If a dialog is shown for minimized window, minimize it too
        if (!init_minimize && win->transient->lead() && !isSessionSaving) {
            bool visible_parent = false;

            for (auto const& lead : win->transient->leads()) {
                if (lead->isShown()) {
                    visible_parent = true;
                }
            }

            if (!visible_parent) {
                init_minimize = true;
                set_demands_attention(win, true);
            }
        }

        if (init_minimize) {
            set_minimized(win, true, true);
        }

        // Window may want to be maximized
        // done after checking that the window isn't larger than the workarea, so that
        // the restore geometry from the checks above takes precedence, and window
        // isn't restored larger than the workarea
        auto maxmode{maximize_mode::restore};

        if (win->net_info->state() & net::MaxVert) {
            maxmode = maxmode | maximize_mode::vertical;
        }
        if (win->net_info->state() & net::MaxHoriz) {
            maxmode = maxmode | maximize_mode::horizontal;
        }

        auto forced_maxmode = win->control->rules.checkMaximize(maxmode, !isMapped);

        // Either hints were set to maximize, or is forced to maximize,
        // or is forced to non-maximize and hints were set to maximize
        if (forced_maxmode != maximize_mode::restore || maxmode != maximize_mode::restore) {
            maximize(win, forced_maxmode);
        }

        // Read other initial states
        set_keep_above(
            win,
            win->control->rules.checkKeepAbove(win->net_info->state() & net::KeepAbove, !isMapped));
        set_keep_below(
            win,
            win->control->rules.checkKeepBelow(win->net_info->state() & net::KeepBelow, !isMapped));
        set_original_skip_taskbar(win,
                                  win->control->rules.checkSkipTaskbar(
                                      win->net_info->state() & net::SkipTaskbar, !isMapped));
        set_skip_pager(
            win,
            win->control->rules.checkSkipPager(win->net_info->state() & net::SkipPager, !isMapped));
        set_skip_switcher(win,
                          win->control->rules.checkSkipSwitcher(
                              win->net_info->state() & net::SkipSwitcher, !isMapped));

        if (win->net_info->state() & net::DemandsAttention) {
            set_demands_attention(win, true);
        }
        if (win->net_info->state() & net::Modal) {
            win->transient->set_modal(true);
        }

        win->setFullScreen(win->control->rules.checkFullScreen(
                               win->net_info->state() & net::FullScreen, !isMapped),
                           false);

        update_allowed_actions(win, true);

        // Set initial user time directly
        win->user_time = read_user_time_map_timestamp(win);

        // And do what Win::updateUserTime() does
        win->group->updateUserTime(win->user_time);

        // This should avoid flicker, because real restacking is done
        // only after manage() finishes because of blocking, but the window is shown sooner
        win->xcb_windows.outer.lower();

        if (!win->space.base.render->compositor->scene) {
            // set to true in case compositing is turned on later. bug #160393
            win->render_data.ready_for_painting = true;
        }

        if (win->isShown()) {
            auto allow = allow_window_activation(space, win, win->userTime(), false);

            // If session saving, force showing new windows (i.e. "save file?" dialogs etc.)
            // also force if activation is allowed
            if (!on_current_desktop(*win) && !isMapped && (allow || isSessionSaving)) {
                space.virtual_desktop_manager->setCurrent(get_desktop(*win));
            }

            if (on_current_desktop(*win) && !isMapped && !allow) {
                restack_client_under_active(win->space, *win);
            }

            update_visibility(win);

            if (!isMapped) {
                if (allow && on_current_desktop(*win)) {
                    if (!is_special_window(win)) {
                        if (space.options->qobject->focusPolicyIsReasonable()
                            && wants_tab_focus(win)) {
                            request_focus(space, *win);
                        }
                    }
                } else if (!is_special_window(win)) {
                    set_demands_attention(win, true);
                }
            }
        } else {
            update_visibility(win);
        }
    }

    assert(win->mapping != mapping_state::withdrawn);

    // Enforce a geometry update now.
    block_geometry_updates(win, false);

    if (decoration(win)) {
        // Sync the final size.
        win->control->deco.client->update_size();
    }

    if (win->user_time == XCB_TIME_CURRENT_TIME || win->user_time == -1U) {
        // No known user time, set something old
        win->user_time = win->space.base.x11_data.time - 1000000;

        // Let's be paranoid.
        if (win->user_time == XCB_TIME_CURRENT_TIME || win->user_time == -1U) {
            win->user_time = win->space.base.x11_data.time - 1000000 + 10;
        }
    }

    // Remove ApplyNow rules
    rules::discard_used_rules(*space.rule_book, *win, false);

    // Was blocked while !control.
    win->updateWindowRules(rules::type::all);

    win->setBlockingCompositing(win->net_info->isBlockingCompositing());
    read_show_on_screen_edge(win, showOnScreenEdgeCookie);

    // Forward all opacity values to the frame in case there'll be other CM running.
    auto comp_qobject = win->space.base.render->compositor->qobject.get();
    using comp_qobject_t = std::remove_pointer_t<decltype(comp_qobject)>;
    QObject::connect(
        comp_qobject, &comp_qobject_t::compositingToggled, win->qobject.get(), [win](bool active) {
            if (active) {
                return;
            }
            if (win->opacity() == 1.0) {
                return;
            }
            net::win_info info(win->space.base.x11_data.connection,
                               win->frameId(),
                               win->space.base.x11_data.root_window,
                               net::Properties(),
                               net::Properties2());
            info.setOpacity(static_cast<unsigned long>(win->opacity() * 0xffffffff));
        });

    add_controlled_window_to_space(space, win);
    return win;
}
}
