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
#include "placement.h"
#include "session.h"
#include "startup_info.h"
#include "user_time.h"
#include "win_info.h"
#include "window_create.h"
#include "xcb.h"

#include "base/logging.h"
#include "win/input.h"
#include "win/layers.h"
#include "win/rules/find.h"
#include "win/session.h"

#include <KStartupInfo>

namespace KWin::win::x11
{

template<typename Win>
void embed_client(Win* win, xcb_visualid_t visualid, xcb_colormap_t colormap, uint8_t depth)
{
    auto xcb_win = static_cast<xcb_window_t>(win->xcb_window);

    assert(xcb_win != XCB_WINDOW_NONE);
    assert(win->xcb_windows.client == XCB_WINDOW_NONE);
    assert(win->frameId() == XCB_WINDOW_NONE);
    assert(win->xcb_windows.wrapper == XCB_WINDOW_NONE);

    win->xcb_windows.client.reset(xcb_win, false);

    uint32_t const zero_value = 0;
    auto conn = connection();

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
        win->space.input->platform.cursor->x11_cursor(Qt::ArrowCursor),
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
                      rootWindow(),
                      0,
                      0,
                      1,
                      1,
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      visualid,
                      cw_mask,
                      cw_values);
    win->xcb_windows.outer.reset(frame);

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

    win->xcb_windows.wrapper.reset(wrapperId);
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

    blocker block(space.stacking_order);

    base::x11::xcb::window_attributes attr(xcb_win);
    base::x11::xcb::geometry windowGeometry(xcb_win);
    if (attr.is_null() || windowGeometry.is_null()) {
        return nullptr;
    }

    auto win = new Win(xcb_win, space);

    // So that decorations don't start with size being (0,0).
    win->set_frame_geometry(QRect(0, 0, 100, 100));

    setup_space_window_connections(&space, win);

    if (auto comp = space.base.render->compositor.get(); comp->x11_integration.update_blocking) {
        QObject::connect(win->qobject.get(),
                         &Win::qobject_t::blockingCompositingChanged,
                         comp->qobject.get(),
                         [comp, win](auto blocks) {
                             comp->x11_integration.update_blocking(blocks ? win : nullptr);
                         });
    }

    QObject::connect(win->qobject.get(),
                     &Win::qobject_t::fullScreenChanged,
                     space.edges->qobject.get(),
                     &screen_edger_qobject::checkBlocking);

    // From this place on, manage() must not return false
    win->control = std::make_unique<typename Win::control_t>(win);

    win->supported_default_types = supported_managed_window_types_mask;
    win->has_in_content_deco = true;

    win->sync_request.timestamp = xTime();

    setup_window_control_connections(win);
    win->control->setup_tabbox();
    win->control->setup_color_scheme();

    QObject::connect(win->client_machine,
                     &client_machine::localhostChanged,
                     win->qobject.get(),
                     [win] { win->updateCaption(); });
    QObject::connect(kwinApp()->options->qobject.get(),
                     &base::options_qobject::configChanged,
                     win->qobject.get(),
                     [win] { win->control->update_mouse_grab(); });
    QObject::connect(kwinApp()->options->qobject.get(),
                     &base::options_qobject::condensedTitleChanged,
                     win->qobject.get(),
                     [win] { win->updateCaption(); });

    QObject::connect(win->qobject.get(),
                     &window_qobject::moveResizeCursorChanged,
                     win->qobject.get(),
                     [win](input::cursor_shape cursor) {
                         auto nativeCursor = win->space.input->platform.cursor->x11_cursor(cursor);
                         win->xcb_windows.outer.define_cursor(nativeCursor);
                         if (win->xcb_windows.input.is_valid()) {
                             win->xcb_windows.input.define_cursor(nativeCursor);
                         }
                         if (win->control->move_resize.enabled) {
                             // changing window attributes doesn't change cursor if there's pointer
                             // grab active
                             xcb_change_active_pointer_grab(
                                 connection(),
                                 nativeCursor,
                                 xTime(),
                                 XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
                                     | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_ENTER_WINDOW
                                     | XCB_EVENT_MASK_LEAVE_WINDOW);
                         }
                     });

    block_geometry_updates(win, true);

    embed_client(win, attr->visual, attr->colormap, windowGeometry->depth);

    win->xcb_visual = attr->visual;
    win->bit_depth = windowGeometry->depth;

    const NET::Properties properties = NET::WMDesktop | NET::WMState | NET::WMWindowType
        | NET::WMStrut | NET::WMName | NET::WMIconGeometry | NET::WMIcon | NET::WMPid
        | NET::WMIconName;
    const NET::Properties2 properties2 = NET::WM2BlockCompositing | NET::WM2WindowClass
        | NET::WM2WindowRole | NET::WM2UserTime | NET::WM2StartupId | NET::WM2ExtendedStrut
        | NET::WM2Opacity | NET::WM2FullscreenMonitors | NET::WM2GroupLeader | NET::WM2Urgency
        | NET::WM2Input | NET::WM2Protocols | NET::WM2InitialMappingState | NET::WM2IconPixmap
        | NET::WM2OpaqueRegion | NET::WM2DesktopFileName | NET::WM2GTKFrameExtents
        | NET::WM2GTKApplicationId;

    auto wmClientLeaderCookie = win->fetchWmClientLeader();
    auto skipCloseAnimationCookie = fetch_skip_close_animation(*win);
    auto showOnScreenEdgeCookie = fetch_show_on_screen_edge(win);
    auto firstInTabBoxCookie = fetch_first_in_tabbox(win);
    auto transientCookie = fetch_transient(win);

    win->geometry_hints.init(win->xcb_window);
    win->motif_hints.init(win->xcb_window);

    win->info
        = new win_info<Win>(win, win->xcb_windows.client, rootWindow(), properties, properties2);

    if (is_desktop(win) && win->bit_depth == 32) {
        // force desktop windows to be opaque. It's a desktop after all, there is no window below
        win->bit_depth = 24;
    }
    win->colormap = attr->colormap;

    win->getResourceClass();
    win->readWmClientLeader(wmClientLeaderCookie);
    win->getWmClientMachine();
    get_sync_counter(win);

    // First only read the caption text, so that win::setup_rules(..) can use it for matching,
    // and only then really set the caption using setCaption(), which checks for duplicates etc.
    // and also relies on rules already existing
    win->caption.normal = read_name(win);

    rules::setup_rules(win, false);
    set_caption(win, win->caption.normal, true);

    QObject::connect(win->qobject.get(),
                     &Win::qobject_t::windowClassChanged,
                     win->qobject.get(),
                     [win] { rules::evaluate_rules(win); });

    if (base::x11::xcb::extensions::self()->is_shape_available()) {
        xcb_shape_select_input(connection(), win->xcb_window, true);
    }

    win->detectShape(win->xcb_window);
    detect_no_border(win);
    fetch_iconic_name(win);

    check_group(win, nullptr);
    update_urgency(win);

    update_allowed_actions(win);

    win->transient()->set_modal((win->info->state() & NET::Modal) != 0);
    read_transient_property(win, transientCookie);

    QByteArray desktopFileName{win->info->desktopFileName()};
    if (desktopFileName.isEmpty()) {
        desktopFileName = win->info->gtkApplicationId();
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
    win->getWmOpaqueRegion();
    win->setSkipCloseAnimation(skipCloseAnimationCookie.to_bool());

    // TODO: Try to obey all state information from info->state()

    set_original_skip_taskbar(win, (win->info->state() & NET::SkipTaskbar) != 0);
    set_skip_pager(win, (win->info->state() & NET::SkipPager) != 0);
    set_skip_switcher(win, (win->info->state() & NET::SkipSwitcher) != 0);
    read_first_in_tabbox(win, firstInTabBoxCookie);

    auto init_minimize = !isMapped && (win->info->initialMappingState() == NET::Iconic);
    if (win->info->state() & NET::Hidden) {
        init_minimize = true;
    }

    KStartupInfoId asn_id;
    KStartupInfoData asn_data;
    auto asn_valid = check_startup_notification(space, win->xcb_window, asn_id, asn_data);

    // Make sure that the input window is created before we update the stacking order
    // TODO(romangg): Does it matter that the frame geometry is not set yet here?
    update_input_window(win, win->frameGeometry());

    update_layer(win);

    auto session = take_session_info(space, win);
    if (session) {
        init_minimize = session->minimized;
        win->user_no_border = session->noBorder;
    }

    set_shortcut(win,
                 win->control->rules.checkShortcut(session ? session->shortcut : QString(), true));

    init_minimize = win->control->rules.checkMinimize(init_minimize, !isMapped);
    win->user_no_border = win->control->rules.checkNoBorder(win->user_no_border, !isMapped);

    // We setup compositing already here so a desktop presence change can access effects.
    win->setupCompositing();

    // Initial desktop placement
    int desk = 0;
    if (session) {
        desk = session->desktop;
        if (session->onAllDesktops) {
            desk = NET::OnAllDesktops;
        }
    } else {
        // If this window is transient, ensure that it is opened on the
        // same window as its parent.  this is necessary when an application
        // starts up on a different desktop than is currently displayed.
        if (win->transient()->lead()) {
            auto leads = win->transient()->leads();
            bool on_current = false;
            bool on_all = false;
            typename Space::window_t* maincl = nullptr;

            // This is slightly duplicated from win::place_on_main_window()
            for (auto const& lead : leads) {
                if (leads.size() > 1 && is_special_window(lead)
                    && !(win->info->state() & NET::Modal)) {
                    // Don't consider group-transients and toolbars etc when placing
                    // except when it's modal (blocks specials as well).
                    continue;
                }

                maincl = lead;
                if (lead->isOnCurrentDesktop()) {
                    on_current = true;
                }
                if (lead->isOnAllDesktops()) {
                    on_all = true;
                }
            }

            if (on_all) {
                desk = NET::OnAllDesktops;
            } else if (on_current) {
                desk = space.virtual_desktop_manager->current();
            } else if (maincl != nullptr) {
                desk = maincl->desktop();
            }
        } else {
            // A transient shall appear on its leader and not drag that around.
            if (win->info->desktop()) {
                // Window had the initial desktop property, force it
                desk = win->info->desktop();
            }
            if (win->desktop() == 0 && asn_valid && asn_data.desktop() != 0) {
                desk = asn_data.desktop();
            }
        }
    }

    if (desk == 0) {
        // Assume window wants to be visible on the current desktop
        desk = is_desktop(win) ? static_cast<int>(NET::OnAllDesktops)
                               : space.virtual_desktop_manager->current();
    }
    desk = win->control->rules.checkDesktop(desk, !isMapped);

    if (desk != NET::OnAllDesktops) {
        // Do range check
        desk = qBound(1, desk, static_cast<int>(space.virtual_desktop_manager->count()));
    }

    set_desktop(win, desk);
    win->info->setDesktop(desk);

    propagate_on_all_desktops_to_children(*win);

    win->client_frame_extents = gtk_frame_extents(win);
    win->geometry_update.original.client_frame_extents = win->client_frame_extents;

    prepare_decoration(win);

    // Set size before placement.
    QRect frame_geo;

    if (session) {
        frame_geo = session->geometry;
    } else {
        auto const client_geo = windowGeometry.rect();

        if (isMapped) {
            win->synced_geometry.client = client_geo;
        }

        auto const frame_pos = client_geo.topLeft() - QPoint(left_border(win), top_border(win))
            + QPoint(win->client_frame_extents.left(), win->client_frame_extents.top());
        auto const frame_size = size_for_client_size(win, client_geo.size(), size_mode::any, false);
        frame_geo = QRect(frame_pos, frame_size);
    }

    win->set_frame_geometry(frame_geo);

    auto const placement_area
        = place_on_taking_control(win, frame_geo, isMapped, session, asn_data);

    // CT: Extra check for stupid jdk 1.3.1. But should make sense in general
    // if client has initial state set to Iconic and is transient with a parent
    // window that is not Iconic, set init_state to Normal
    if (init_minimize) {
        auto leads = win->transient()->leads();
        for (auto lead : leads) {
            if (lead->isShown()) {
                // SELI TODO: Even e.g. for NET::Utility?
                init_minimize = false;
            }
        }
    }

    // If a dialog is shown for minimized window, minimize it too
    if (!init_minimize && win->transient()->lead()
        && space.session_manager->state() != SessionState::Saving) {
        bool visible_parent = false;

        for (auto const& lead : win->transient()->leads()) {
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

    // Other settings from the previous session
    if (session) {
        // Session restored windows are not considered to be new windows WRT rules,
        // I.e. obey only forcing rules
        set_keep_above(win, session->keepAbove);
        set_keep_below(win, session->keepBelow);
        set_original_skip_taskbar(win, session->skipTaskbar);
        set_skip_pager(win, session->skipPager);
        set_skip_switcher(win, session->skipSwitcher);
        win->setOpacity(session->opacity);

        if (static_cast<maximize_mode>(session->maximized) != maximize_mode::restore) {
            maximize(win, static_cast<maximize_mode>(session->maximized));
            win->restore_geometries.maximize = session->restore;
        }
        if (session->fullscreen) {
            win->setFullScreen(true, false);
            win->restore_geometries.maximize = session->fsrestore;
        }

        check_offscreen_position(win->restore_geometries.maximize, placement_area);

    } else {
        // Window may want to be maximized
        // done after checking that the window isn't larger than the workarea, so that
        // the restore geometry from the checks above takes precedence, and window
        // isn't restored larger than the workarea
        auto maxmode{maximize_mode::restore};

        if (win->info->state() & NET::MaxVert) {
            maxmode = maxmode | maximize_mode::vertical;
        }
        if (win->info->state() & NET::MaxHoriz) {
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
            win->control->rules.checkKeepAbove(win->info->state() & NET::KeepAbove, !isMapped));
        set_keep_below(
            win,
            win->control->rules.checkKeepBelow(win->info->state() & NET::KeepBelow, !isMapped));
        set_original_skip_taskbar(
            win,
            win->control->rules.checkSkipTaskbar(win->info->state() & NET::SkipTaskbar, !isMapped));
        set_skip_pager(
            win,
            win->control->rules.checkSkipPager(win->info->state() & NET::SkipPager, !isMapped));
        set_skip_switcher(win,
                          win->control->rules.checkSkipSwitcher(
                              win->info->state() & NET::SkipSwitcher, !isMapped));

        if (win->info->state() & NET::DemandsAttention) {
            set_demands_attention(win, true);
        }
        if (win->info->state() & NET::Modal) {
            win->transient()->set_modal(true);
        }

        win->setFullScreen(
            win->control->rules.checkFullScreen(win->info->state() & NET::FullScreen, !isMapped),
            false);
    }

    update_allowed_actions(win, true);

    // Set initial user time directly
    win->user_time = read_user_time_map_timestamp(
        win, asn_valid ? &asn_id : nullptr, asn_valid ? &asn_data : nullptr, session);

    // And do what Win::updateUserTime() does
    win->group()->updateUserTime(win->user_time);

    // This should avoid flicker, because real restacking is done
    // only after manage() finishes because of blocking, but the window is shown sooner
    win->xcb_windows.outer.lower();

    if (session && session->stackingOrder != -1) {
        win->sm_stacking_order = session->stackingOrder;
        restore_session_stacking_order(&space, win);
    }

    if (!win->space.base.render->compositor->scene) {
        // set to true in case compositing is turned on later. bug #160393
        win->ready_for_painting = true;
    }

    if (win->isShown()) {
        bool allow;
        if (session) {
            allow = session->active
                && (!space.was_user_interaction || !space.active_client
                    || is_desktop(space.active_client));
        } else {
            allow = allow_window_activation(space, win, win->userTime(), false);
        }

        auto const isSessionSaving = space.session_manager->state() == SessionState::Saving;

        // If session saving, force showing new windows (i.e. "save file?" dialogs etc.)
        // also force if activation is allowed
        if (!win->isOnCurrentDesktop() && !isMapped && !session && (allow || isSessionSaving)) {
            space.virtual_desktop_manager->setCurrent(win->desktop());
        }

        if (win->isOnCurrentDesktop() && !isMapped && !allow
            && (!session || session->stackingOrder < 0)) {
            restack_client_under_active(&win->space, win);
        }

        update_visibility(win);

        if (!isMapped) {
            if (allow && win->isOnCurrentDesktop()) {
                if (!is_special_window(win)) {
                    if (kwinApp()->options->qobject->focusPolicyIsReasonable()
                        && wants_tab_focus(win)) {
                        request_focus(space, win);
                    }
                }
            } else if (!session && !is_special_window(win)) {
                set_demands_attention(win, true);
            }
        }
    } else {
        update_visibility(win);
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
        win->user_time = xTime() - 1000000;

        // Let's be paranoid.
        if (win->user_time == XCB_TIME_CURRENT_TIME || win->user_time == -1U) {
            win->user_time = xTime() - 1000000 + 10;
        }
    }

    delete session;

    win->control->discard_temporary_rules();

    // Remove ApplyNow rules
    rules::discard_used_rules(*space.rule_book, *win, false);

    // Was blocked while !control.
    win->updateWindowRules(rules::type::all);

    win->setBlockingCompositing(win->info->isBlockingCompositing());
    read_show_on_screen_edge(win, showOnScreenEdgeCookie);

    // Forward all opacity values to the frame in case there'll be other CM running.
    QObject::connect(
        win->space.base.render->compositor->qobject.get(),
        &render::compositor_qobject::compositingToggled,
        win->qobject.get(),
        [win](bool active) {
            if (active) {
                return;
            }
            if (win->opacity() == 1.0) {
                return;
            }
            NETWinInfo info(
                connection(), win->frameId(), rootWindow(), NET::Properties(), NET::Properties2());
            info.setOpacity(static_cast<unsigned long>(win->opacity() * 0xffffffff));
        });

    add_controlled_window_to_space(space, win);
    return win;
}

template<typename Win>
xcb_timestamp_t read_user_time_map_timestamp(Win* win,
                                             const KStartupInfoId* asn_id,
                                             const KStartupInfoData* asn_data,
                                             bool session)
{
    xcb_timestamp_t time = win->info->userTime();

    // newer ASN timestamp always replaces user timestamp, unless user timestamp is 0
    // helps e.g. with konqy reusing
    if (asn_data != nullptr && time != 0) {
        if (asn_id->timestamp() != 0
            && (time == -1U || NET::timestampCompare(asn_id->timestamp(), time) > 0)) {
            time = asn_id->timestamp();
        }
    }
    qCDebug(KWIN_CORE) << "User timestamp, ASN:" << time;
    if (time == -1U) {
        // The window doesn't have any timestamp.
        // If it's the first window for its application
        // (i.e. there's no other window from the same app),
        // use the _KDE_NET_WM_USER_CREATION_TIME trick.
        // Otherwise, refuse activation of a window
        // from already running application if this application
        // is not the active one (unless focus stealing prevention is turned off).
        auto act = dynamic_cast<Win*>(most_recently_activated_window(win->space));
        if (act != nullptr
            && !belong_to_same_application(act, win, same_client_check::relaxed_for_active)) {
            bool first_window = true;
            auto sameApplicationActiveHackPredicate = [win](auto const* cl) {
                // ignore already existing splashes, toolbars, utilities and menus,
                // as the app may show those before the main window
                auto x11_client = dynamic_cast<Win const*>(cl);
                return x11_client && !is_splash(x11_client) && !is_toolbar(x11_client)
                    && !is_utility(x11_client) && !is_menu(x11_client) && x11_client != win
                    && belong_to_same_application(
                           x11_client, win, same_client_check::relaxed_for_active);
            };
            if (win->transient()->lead()) {
                auto clientMainClients = [win]() {
                    std::vector<Win*> ret;
                    const auto mcs = win->transient()->leads();
                    for (auto mc : mcs) {
                        if (auto c = dynamic_cast<Win*>(mc)) {
                            ret.push_back(c);
                        }
                    }
                    return ret;
                };
                if (win->transient()->is_follower_of(act))
                    ; // is transient for currently active window, even though it's not
                // the same app (e.g. kcookiejar dialog) -> allow activation
                else if (win->groupTransient()
                         && find_in_list<Win, Win>(clientMainClients(),
                                                   sameApplicationActiveHackPredicate)
                             == nullptr)
                    ; // standalone transient
                else
                    first_window = false;
            } else {
                for (auto win : win->space.windows) {
                    if (win->control && sameApplicationActiveHackPredicate(win)) {
                        first_window = false;
                        break;
                    }
                }
            }
            // don't refuse if focus stealing prevention is turned off
            if (!first_window
                && enum_index(win->control->rules.checkFSP(
                       kwinApp()->options->qobject->focusStealingPreventionLevel()))
                    > 0) {
                qCDebug(KWIN_CORE) << "User timestamp, already exists:" << 0;
                return 0; // refuse activation
            }
        }
        // Creation time would just mess things up during session startup,
        // as possibly many apps are started up at the same time.
        // If there's no active window yet, no timestamp will be needed,
        // as plain allow_window_activation() will return true
        // in such case. And if there's already active window,
        // it's better not to activate the new one.
        // Unless it was the active window at the time
        // of session saving and there was no user interaction yet,
        // this check will be done in manage().
        if (session)
            return -1U;
        time = read_user_creation_time(win);
    }
    qCDebug(KWIN_CORE) << "User timestamp, final:" << win << ":" << time;
    return time;
}

}
