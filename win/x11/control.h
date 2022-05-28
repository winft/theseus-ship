/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "client_machine.h"
#include "command.h"
#include "deco.h"
#include "geo.h"
#include "meta.h"
#include "netinfo.h"
#include "space.h"
#include "window.h"
#include "window_create.h"
#include "window_find.h"
#include "xcb.h"

#include "base/logging.h"
#include "base/x11/xcb/proto.h"
#include "rules/rule_book.h"
#include "utils/blocker.h"
#include "utils/geo.h"
#include "win/control.h"
#include "win/controlling.h"
#include "win/input.h"
#include "win/layers.h"
#include "win/meta.h"
#include "win/placement.h"
#include "win/screen.h"
#include "win/setup.h"
#include "win/space_helpers.h"
#include "win/stacking.h"
#include "win/stacking_order.h"
#include "win/util.h"

#if KWIN_BUILD_TABBOX
#include "win/tabbox/tabbox.h"
#endif

#include <KStartupInfo>

namespace KWin::win::x11
{

template<typename Win>
class x11_control : public win::control
{
public:
    x11_control(Win* window)
        : win::control(window)
        , m_window{window}
    {
    }

    void set_skip_pager(bool set) override
    {
        win::control::set_skip_pager(set);
        m_window->info->setState(skip_pager() ? NET::SkipPager : NET::States(), NET::SkipPager);
    }

    void set_skip_switcher(bool set) override
    {
        win::control::set_skip_switcher(set);
        m_window->info->setState(skip_switcher() ? NET::SkipSwitcher : NET::States(),
                                 NET::SkipSwitcher);
    }

    void set_skip_taskbar(bool set) override
    {
        win::control::set_skip_taskbar(set);
        m_window->info->setState(skip_taskbar() ? NET::SkipTaskbar : NET::States(),
                                 NET::SkipTaskbar);
    }

    void update_mouse_grab() override
    {
        xcb_ungrab_button(
            connection(), XCB_BUTTON_INDEX_ANY, m_window->xcb_windows.wrapper, XCB_MOD_MASK_ANY);

        if (m_window->space.tabbox->forced_global_mouse_grab()) {
            // see TabBox::establishTabBoxGrab()
            m_window->xcb_windows.wrapper.grab_button(XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_ASYNC);
            return;
        }

        // When a passive grab is activated or deactivated, the X server will generate crossing
        // events as if the pointer were suddenly to warp from its current position to some position
        // in the grab window. Some /broken/ X11 clients do get confused by such EnterNotify and
        // LeaveNotify events so we release the passive grab for the active window.
        //
        // The passive grab below is established so the window can be raised or activated when it
        // is clicked.
        if ((kwinApp()->options->focusPolicyIsReasonable() && !active())
            || (kwinApp()->options->isClickRaise() && !is_most_recently_raised(m_window))) {
            if (kwinApp()->options->commandWindow1() != base::options::MouseNothing) {
                establish_command_window_grab(m_window, XCB_BUTTON_INDEX_1);
            }
            if (kwinApp()->options->commandWindow2() != base::options::MouseNothing) {
                establish_command_window_grab(m_window, XCB_BUTTON_INDEX_2);
            }
            if (kwinApp()->options->commandWindow3() != base::options::MouseNothing) {
                establish_command_window_grab(m_window, XCB_BUTTON_INDEX_3);
            }
            if (kwinApp()->options->commandWindowWheel() != base::options::MouseNothing) {
                establish_command_window_grab(m_window, XCB_BUTTON_INDEX_4);
                establish_command_window_grab(m_window, XCB_BUTTON_INDEX_5);
            }
        }

        // We want to grab <command modifier> + buttons no matter what state the window is in. The
        // client will receive funky EnterNotify and LeaveNotify events, but there is nothing that
        // we can do about it, unfortunately.

        if (!m_window->space.globalShortcutsDisabled()) {
            if (kwinApp()->options->commandAll1() != base::options::MouseNothing) {
                establish_command_all_grab(m_window, XCB_BUTTON_INDEX_1);
            }
            if (kwinApp()->options->commandAll2() != base::options::MouseNothing) {
                establish_command_all_grab(m_window, XCB_BUTTON_INDEX_2);
            }
            if (kwinApp()->options->commandAll3() != base::options::MouseNothing) {
                establish_command_all_grab(m_window, XCB_BUTTON_INDEX_3);
            }
            if (kwinApp()->options->commandAllWheel() != base::options::MouseWheelNothing) {
                establish_command_all_grab(m_window, XCB_BUTTON_INDEX_4);
                establish_command_all_grab(m_window, XCB_BUTTON_INDEX_5);
            }
        }
    }

    void destroy_decoration() override
    {
        if (decoration(m_window)) {
            auto const grav = calculate_gravitation(m_window, true);
            win::control::destroy_decoration();
            move(m_window, grav);
        }
        m_window->xcb_windows.input.reset();
    }

    QSize adjusted_frame_size(QSize const& frame_size, size_mode mode) override
    {
        auto const client_size = frame_to_client_size(m_window, frame_size);
        return size_for_client_size(m_window, client_size, mode, false);
    }

    bool can_fullscreen() const override
    {
        if (!rules().checkFullScreen(true)) {
            return false;
        }
        if (rules().checkStrictGeometry(true)) {
            // check geometry constraints (rule to obey is set)
            const QRect fsarea = m_window->space.clientArea(FullScreenArea, m_window);
            if (size_for_client_size(m_window, fsarea.size(), win::size_mode::any, true)
                != fsarea.size()) {
                // the app wouldn't fit exactly fullscreen geometry due to its strict geometry
                // requirements
                return false;
            }
        }
        // don't check size constrains - some apps request fullscreen despite requesting fixed size
        // also better disallow weird types to go fullscreen
        return !is_special_window(m_window);
    }

private:
    Win* m_window;
};

template<typename Win>
bool has_user_time_support(Win* win)
{
    return win->info->userTime() != -1U;
}

template<typename Win>
void embed_client(Win* win,
                  xcb_window_t w,
                  xcb_visualid_t visualid,
                  xcb_colormap_t colormap,
                  uint8_t depth)
{
    assert(win->xcb_windows.client == XCB_WINDOW_NONE);
    assert(win->frameId() == XCB_WINDOW_NONE);
    assert(win->xcb_windows.wrapper == XCB_WINDOW_NONE);
    win->xcb_windows.client.reset(w, false);

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
        input::get_cursor()->x11_cursor(Qt::ArrowCursor),
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

    win->setWindowHandles(win->xcb_windows.client);

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

/**
 * Checks if the window provides its own placement via geometry hint and we want to use it or if
 * this is overriden by us (via window rule).
 */
template<typename Win>
bool position_via_hint(Win* win, QRect const& geo, bool ignore_default, QRect& place_area)
{
    if (win->control->rules().checkIgnoreGeometry(ignore_default, true)) {
        // Hint is to be ignored via rule.
        return false;
    }
    if (!win->geometry_hints.has_position()) {
        return false;
    }

    // Window provides its own placement via geometry hint.

    // Disobey xinerama placement option for now (#70943)
    place_area = win->space.clientArea(PlacementArea, geo.center(), win->desktop());

    return true;
}

template<typename Win>
bool move_with_force_rule(Win* win, QRect& frame_geo, bool is_inital_placement, QRect& area)
{
    auto forced_pos = win->control->rules().checkPosition(geo::invalid_point, is_inital_placement);

    if (forced_pos == geo::invalid_point) {
        return false;
    }

    move(win, forced_pos);
    frame_geo = pending_frame_geometry(win);

    // Don't keep inside workarea if the window has specially configured position
    area = win->space.clientArea(FullArea, frame_geo.center(), win->desktop());
    return true;
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
void resize_on_taking_control(Win* win, QRect& frame_geo, bool mapped)
{
    // TODO: Is CentralGravity right here, when resizing is done after gravitating?
    auto const adj_frame_size = adjusted_frame_size(win, frame_geo.size(), size_mode::any);
    auto const rule_checked_size = win->control->rules().checkSize(adj_frame_size, !mapped);
    win->setFrameGeometry(QRect(win->pos(), rule_checked_size));
    frame_geo = pending_frame_geometry(win);
}

template<typename Win>
QRect keep_in_placement_area(Win* win, QRect const& area, bool partial)
{
    auto impl = [&]() {
        if (is_special_window(win) || is_toolbar(win)) {
            return;
        }
        if (!win->isMovable()) {
            return;
        }
        keep_in_area(win, area, partial);
    };

    impl();
    return pending_frame_geometry(win);
}

template<typename Win>
void place_max_fs(Win* win,
                  QRect& frame_geo,
                  QRect const& area,
                  bool keep_in_area,
                  bool partial_keep_in_area)
{
    if (!win->isMaximizable()) {
        frame_geo = keep_in_placement_area(win, area, partial_keep_in_area);
        return;
    }
    if (win->size().width() < area.width() && win->size().height() < area.height()) {
        // Window smaller than the screen, do not maximize.
        frame_geo = keep_in_placement_area(win, area, partial_keep_in_area);
        return;
    }

    auto const screen_area
        = win->space.clientArea(ScreenArea, area.center(), win->desktop()).size();
    auto const full_area = win->space.clientArea(FullArea, frame_geo.center(), win->desktop());
    auto const client_size = frame_to_client_size(win, win->size());

    auto pseudo_max{maximize_mode::restore};

    if (win->info->state() & NET::MaxVert) {
        pseudo_max |= maximize_mode::vertical;
    }
    if (win->info->state() & NET::MaxHoriz) {
        pseudo_max |= maximize_mode::horizontal;
    }

    if (win->size().width() >= area.width()) {
        pseudo_max |= maximize_mode::horizontal;
    }
    if (win->size().height() >= area.height()) {
        pseudo_max |= maximize_mode::vertical;
    }

    // Heuristic: If a decorated client is smaller than the entire screen, the user might want to
    // move it around (multiscreen) in this case, if the decorated client is bigger than the screen
    // (+1), we don't take this as an attempt for maximization, but just constrain the size
    // (the window simply wants to be bigger).
    auto keep_in_fullscreen_area{false};

    if (win->size().width() < full_area.width()
        && (client_size.width() > screen_area.width() + 1)) {
        pseudo_max = pseudo_max & ~maximize_mode::horizontal;
        keep_in_fullscreen_area = true;
    }
    if (win->size().height() < full_area.height()
        && (client_size.height() > screen_area.height() + 1)) {
        pseudo_max = pseudo_max & ~maximize_mode::vertical;
        keep_in_fullscreen_area = true;
    }

    if (pseudo_max != maximize_mode::restore) {
        maximize(win, pseudo_max);
        assert(win->geometry_update.max_mode == pseudo_max);

        // from now on, care about maxmode, since the maximization call will override mode
        // for fix aspects
        keep_in_area &= pseudo_max != maximize_mode::full;

        if (pseudo_max == maximize_mode::full) {
            // Unset restore geometry. On unmaximize we set to a default size and placement.
            win->restore_geometries.maximize = QRect();
        } else if (flags(pseudo_max & maximize_mode::vertical)) {
            // Only vertically maximized. Restore horizontal axis only and choose some default
            // restoration for the vertical axis.
            assert(!(pseudo_max & maximize_mode::horizontal));
            auto restore_height = screen_area.height() * 2 / 3.;
            auto restore_y = (screen_area.height() - restore_height) / 2;
            win->restore_geometries.maximize.setY(restore_y);
            win->restore_geometries.maximize.setHeight(restore_height);
        } else {
            // Horizontally maximized only.
            assert(flags(pseudo_max & maximize_mode::horizontal));
            auto restore_width = screen_area.width() * 2 / 3.;
            auto restore_x = (screen_area.width() - restore_width) / 2;
            win->restore_geometries.maximize.setX(restore_x);
            win->restore_geometries.maximize.setWidth(restore_width);
        }
    }

    if (keep_in_fullscreen_area) {
        win::keep_in_area(win, full_area, partial_keep_in_area);
    }
    if (keep_in_area) {
        keep_in_placement_area(win, area, partial_keep_in_area);
    }
    frame_geo = pending_frame_geometry(win);
}

template<typename Win>
bool must_correct_position(Win* win, QRect const& geo, QRect const& area)
{
    return win->isMovable() && (geo.x() > area.right() || geo.y() > area.bottom());
}

template<typename Win>
QRect place_mapped(Win* win, QRect& frame_geo)
{
    auto must_place{false};

    auto area = win->space.clientArea(FullArea, frame_geo.center(), win->desktop());
    check_offscreen_position(frame_geo, area);

    if (must_correct_position(win, frame_geo, area)) {
        must_place = true;
    }

    if (!must_place) {
        // No standard placement required, just move and optionally force placement and return.
        move(win, frame_geo.topLeft());
        resize_on_taking_control(win, frame_geo, true);
        move_with_force_rule(win, frame_geo, false, area);
        place_max_fs(win, frame_geo, area, false, true);
        return area;
    }

    resize_on_taking_control(win, frame_geo, true);

    if (move_with_force_rule(win, frame_geo, false, area)) {
        // Placement overriden with force rule.
        place_max_fs(win, frame_geo, area, true, true);
        return area;
    }

    win::place(win, area);
    frame_geo = pending_frame_geometry(win);

    // The client may have been moved to another screen, update placement area.
    area = win->space.clientArea(PlacementArea, win);

    place_max_fs(win, frame_geo, area, false, true);
    return area;
}

template<typename Win>
QRect place_session(Win* win, QRect& frame_geo)
{
    auto must_place{false};

    auto area = win->space.clientArea(FullArea, frame_geo.center(), win->desktop());
    check_offscreen_position(frame_geo, area);

    if (must_correct_position(win, frame_geo, area)) {
        must_place = true;
    }

    if (!must_place) {
        // Move instead of further placement.
        // Session contains the position of the frame geometry before gravitating.
        move(win, frame_geo.topLeft());
        resize_on_taking_control(win, frame_geo, true);
        move_with_force_rule(win, frame_geo, true, area);
        frame_geo = keep_in_placement_area(win, area, true);
        return area;
    }

    resize_on_taking_control(win, frame_geo, true);

    if (move_with_force_rule(win, frame_geo, true, area)) {
        // Placement overriden with force rule.
        frame_geo = keep_in_placement_area(win, area, true);
        return area;
    }

    win::place(win, area);
    frame_geo = pending_frame_geometry(win);

    // The client may have been moved to another screen, update placement area.
    area = win->space.clientArea(PlacementArea, win);
    frame_geo = keep_in_placement_area(win, area, true);
    return area;
}

template<typename Win>
bool ignore_position_default(Win* win)
{
    // TODO(romangg): This function flow can surely be radically simplified.
    if (win->transient()->lead()) {
        if (!is_utility(win) && !is_dialog(win) && !is_splash(win)) {
            return false;
        }
        if (!win->info->hasNETSupport()) {
            return false;
        }
        // TODO(romangg): Should we return false here?
    }
    if (is_dialog(win) && win->info->hasNETSupport()) {
        return false;
    }
    if (is_on_screen_display(win)) {
        return true;
    }
    if (is_splash(win)) {
        return true;
    }
    return false;
}

template<typename Win>
QRect place_unmapped(Win* win, QRect& frame_geo, KStartupInfoData const& asn_data)
{
    auto const& base = kwinApp()->get_base();
    auto output = asn_data.xinerama() == -1
        ? get_current_output(win->space)
        : base::get_output(base.get_outputs(), asn_data.xinerama());

    QPoint center;
    if (output) {
        output = win->control->rules().checkScreen(output, true);
        center = output->geometry().center();
    }

    auto area = win->space.clientArea(PlacementArea, center, win->desktop());

    // Desktop windows' positions are not placed by us.
    auto must_place = !is_desktop(win);

    if (position_via_hint(win, frame_geo, ignore_position_default(win), area)) {
        must_place = false;
    }

    if (!must_place) {
        move(win, frame_geo.topLeft());
    }

    resize_on_taking_control(win, frame_geo, false);

    if (move_with_force_rule(win, frame_geo, true, area)) {
        // Placement overriden with force rule.
        place_max_fs(win, frame_geo, area, true, false);
        return area;
    }

    if (must_place) {
        win::place(win, area);
        frame_geo = pending_frame_geometry(win);

        // The client may have been moved to another screen, update placement area.
        area = win->space.clientArea(PlacementArea, win);
    }

    place_max_fs(win, frame_geo, area, false, false);

    return area;
}

template<typename Win>
QRect place_on_taking_control(Win* win,
                              QRect& frame_geo,
                              bool mapped,
                              win::session_info* session,
                              KStartupInfoData const& asn_data)
{
    if (session) {
        if (mapped) {
            qCWarning(KWIN_CORE)
                << "Unexpected client behavior: session info provided for already mapped client.";
        }
        return place_session(win, frame_geo);
    }
    if (mapped) {
        return place_mapped(win, frame_geo);
    }

    return place_unmapped(win, frame_geo, asn_data);
}

/**
 * Manages the clients. This means handling the very first maprequest:
 * reparenting, initial geometry, initial state, placement, etc.
 * Returns false if KWin is not going to manage this window.
 */
template<typename Space>
auto create_controlled_window(xcb_window_t w, bool isMapped, Space& space) ->
    typename Space::x11_window*
{
    using Win = typename Space::x11_window;

    blocker block(space.stacking_order);

    base::x11::xcb::window_attributes attr(w);
    base::x11::xcb::geometry windowGeometry(w);
    if (attr.is_null() || windowGeometry.is_null()) {
        return nullptr;
    }

    auto win = new Win(space);

    // So that decorations don't start with size being (0,0).
    win->set_frame_geometry(QRect(0, 0, 100, 100));

    setup_space_window_connections(&space, win);

    if (auto& comp = space.render; comp.x11_integration.update_blocking) {
        QObject::connect(win,
                         &win::x11::window::blockingCompositingChanged,
                         &comp,
                         [&comp](auto window) { comp.x11_integration.update_blocking(window); });
    }

    QObject::connect(win,
                     &win::x11::window::client_fullscreen_set,
                     space.edges.get(),
                     &screen_edger::checkBlocking);

    // From this place on, manage() must not return false
    win->control.reset(new x11_control(win));

    win->supported_default_types = supported_managed_window_types_mask;
    win->has_in_content_deco = true;

    win->sync_request.timestamp = xTime();

    setup_window_control_connections(win);
    win->control->setup_tabbox();
    win->control->setup_color_scheme();

    QObject::connect(
        win->clientMachine(), &client_machine::localhostChanged, win, &window::updateCaption);
    QObject::connect(kwinApp()->options.get(), &base::options::configChanged, win, [win] {
        win->control->update_mouse_grab();
    });
    QObject::connect(kwinApp()->options.get(),
                     &base::options::condensedTitleChanged,
                     win,
                     &window::updateCaption);

    QObject::connect(win, &window::moveResizeCursorChanged, win, [win](input::cursor_shape cursor) {
        auto nativeCursor = input::get_cursor()->x11_cursor(cursor);
        win->xcb_windows.outer.define_cursor(nativeCursor);
        if (win->xcb_windows.input.is_valid()) {
            win->xcb_windows.input.define_cursor(nativeCursor);
        }
        if (win->control->move_resize().enabled) {
            // changing window attributes doesn't change cursor if there's pointer grab active
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

    embed_client(win, w, attr->visual, attr->colormap, windowGeometry->depth);

    win->m_visual = attr->visual;
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

    win->geometry_hints.init(win->xcb_window());
    win->motif_hints.init(win->xcb_window());

    win->info = new win_info(win, win->xcb_windows.client, rootWindow(), properties, properties2);

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

    setup_rules(win, false);
    set_caption(win, win->caption.normal, true);

    QObject::connect(win, &Win::windowClassChanged, win, [win] { evaluate_rules(win); });

    if (base::x11::xcb::extensions::self()->is_shape_available()) {
        xcb_shape_select_input(connection(), win->xcb_window(), true);
    }

    win->detectShape(win->xcb_window());
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
                          win->control->rules().checkDesktopFile(desktopFileName, true).toUtf8());
    get_icons(win);
    QObject::connect(win, &window::desktopFileNameChanged, win, [win] { get_icons(win); });

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
    auto asn_valid = space.checkStartupNotification(win->xcb_window(), asn_id, asn_data);

    // Make sure that the input window is created before we update the stacking order
    // TODO(romangg): Does it matter that the frame geometry is not set yet here?
    update_input_window(win, win->frameGeometry());

    update_layer(win);

    auto session = space.takeSessionInfo(win);
    if (session) {
        init_minimize = session->minimized;
        win->user_no_border = session->noBorder;
    }

    set_shortcut(
        win, win->control->rules().checkShortcut(session ? session->shortcut : QString(), true));

    init_minimize = win->control->rules().checkMinimize(init_minimize, !isMapped);
    win->user_no_border = win->control->rules().checkNoBorder(win->user_no_border, !isMapped);

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
            Toplevel* maincl = nullptr;

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
    desk = win->control->rules().checkDesktop(desk, !isMapped);

    if (desk != NET::OnAllDesktops) {
        // Do range check
        desk = qBound(1, desk, static_cast<int>(space.virtual_desktop_manager->count()));
    }

    set_desktop(win, desk);
    win->info->setDesktop(desk);

    space.updateOnAllDesktopsOfTransients(win);

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

        auto forced_maxmode = win->control->rules().checkMaximize(maxmode, !isMapped);

        // Either hints were set to maximize, or is forced to maximize,
        // or is forced to non-maximize and hints were set to maximize
        if (forced_maxmode != maximize_mode::restore || maxmode != maximize_mode::restore) {
            maximize(win, forced_maxmode);
        }

        // Read other initial states
        set_keep_above(
            win,
            win->control->rules().checkKeepAbove(win->info->state() & NET::KeepAbove, !isMapped));
        set_keep_below(
            win,
            win->control->rules().checkKeepBelow(win->info->state() & NET::KeepBelow, !isMapped));
        set_original_skip_taskbar(win,
                                  win->control->rules().checkSkipTaskbar(
                                      win->info->state() & NET::SkipTaskbar, !isMapped));
        set_skip_pager(
            win,
            win->control->rules().checkSkipPager(win->info->state() & NET::SkipPager, !isMapped));
        set_skip_switcher(win,
                          win->control->rules().checkSkipSwitcher(
                              win->info->state() & NET::SkipSwitcher, !isMapped));

        if (win->info->state() & NET::DemandsAttention) {
            set_demands_attention(win, true);
        }
        if (win->info->state() & NET::Modal) {
            win->transient()->set_modal(true);
        }

        win->setFullScreen(
            win->control->rules().checkFullScreen(win->info->state() & NET::FullScreen, !isMapped),
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

    if (!win->space.compositing()) {
        // set to true in case compositing is turned on later. bug #160393
        win->ready_for_painting = true;
    }

    if (win->isShown()) {
        bool allow;
        if (session) {
            allow = session->active
                && (!space.wasUserInteraction() || space.activeClient() == nullptr
                    || is_desktop(space.activeClient()));
        } else {
            allow = space.allowClientActivation(win, win->userTime(), false);
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
                    if (kwinApp()->options->focusPolicyIsReasonable() && wants_tab_focus(win)) {
                        space.request_focus(win);
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
        win->control->deco().client->update_size();
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
    space.rule_book->discardUsed(win, false);

    // Was blocked while !control.
    win->updateWindowRules(Rules::All);

    win->setBlockingCompositing(win->info->isBlockingCompositing());
    read_show_on_screen_edge(win, showOnScreenEdgeCookie);

    // Forward all opacity values to the frame in case there'll be other CM running.
    QObject::connect(
        &win->space.render, &render::compositor::compositingToggled, win, [win](bool active) {
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

template<typename Space, typename Win>
void lower_client_within_application(Space* space, Win* window)
{
    if (!window) {
        return;
    }

    window->control->cancel_auto_raise();

    blocker block(space->stacking_order);

    remove_all(space->stacking_order->pre_stack, window);

    bool lowered = false;
    // first try to put it below the bottom-most window of the application
    for (auto it = space->stacking_order->pre_stack.begin();
         it != space->stacking_order->pre_stack.end();
         ++it) {
        auto const& client = *it;
        if (!client) {
            continue;
        }
        if (win::belong_to_same_client(client, window)) {
            space->stacking_order->pre_stack.insert(it, window);
            lowered = true;
            break;
        }
    }
    if (!lowered)
        space->stacking_order->pre_stack.push_front(window);
    // ignore mainwindows
}

template<typename Space, typename Win>
void raise_client_within_application(Space* space, Win* window)
{
    if (!window) {
        return;
    }

    window->control->cancel_auto_raise();

    blocker block(space->stacking_order);
    // ignore mainwindows

    // first try to put it above the top-most window of the application
    for (int i = space->stacking_order->pre_stack.size() - 1; i > -1; --i) {
        auto other = space->stacking_order->pre_stack.at(i);
        if (!other) {
            continue;
        }
        if (other == window) {
            // Don't lower it just because it asked to be raised.
            return;
        }
        if (belong_to_same_client(other, window)) {
            remove_all(space->stacking_order->pre_stack, window);
            auto it = find(space->stacking_order->pre_stack, other);
            assert(it != space->stacking_order->pre_stack.end());
            // Insert after the found one.
            space->stacking_order->pre_stack.insert(it + 1, window);
            break;
        }
    }
}

template<typename Space, typename Win>
void raise_client_request(Space* space,
                          Win* c,
                          NET::RequestSource src = NET::FromApplication,
                          xcb_timestamp_t timestamp = 0)
{
    if (src == NET::FromTool || space->allowFullClientRaising(c, timestamp)) {
        raise_window(space, c);
    } else {
        raise_client_within_application(space, c);
        set_demands_attention(c, true);
    }
}

template<typename Space, typename Win>
void lower_client_request(Space* space,
                          Win* c,
                          NET::RequestSource src,
                          [[maybe_unused]] xcb_timestamp_t /*timestamp*/)
{
    // If the client has support for all this focus stealing prevention stuff,
    // do only lowering within the application, as that's the more logical
    // variant of lowering when application requests it.
    // No demanding of attention here of course.
    if (src == NET::FromTool || !has_user_time_support(c)) {
        lower_window(space, c);
    } else {
        lower_client_within_application(space, c);
    }
}

template<typename Win>
void restack_window(Win* win,
                    xcb_window_t above,
                    int detail,
                    NET::RequestSource src,
                    xcb_timestamp_t timestamp,
                    bool send_event = false)
{
    Win* other = nullptr;
    if (detail == XCB_STACK_MODE_OPPOSITE) {
        other
            = find_controlled_window<win::x11::window>(win->space, predicate_match::window, above);
        if (!other) {
            raise_or_lower_client(&win->space, win);
            return;
        }

        auto it = win->space.stacking_order->sorted().cbegin();
        auto end = win->space.stacking_order->sorted().cend();

        while (it != end) {
            if (*it == win) {
                detail = XCB_STACK_MODE_ABOVE;
                break;
            } else if (*it == other) {
                detail = XCB_STACK_MODE_BELOW;
                break;
            }
            ++it;
        }
    } else if (detail == XCB_STACK_MODE_TOP_IF) {
        other
            = find_controlled_window<win::x11::window>(win->space, predicate_match::window, above);
        if (other && other->frameGeometry().intersects(win->frameGeometry())) {
            raise_client_request(&win->space, win, src, timestamp);
        }
        return;
    } else if (detail == XCB_STACK_MODE_BOTTOM_IF) {
        other
            = find_controlled_window<win::x11::window>(win->space, predicate_match::window, above);
        if (other && other->frameGeometry().intersects(win->frameGeometry())) {
            lower_client_request(&win->space, win, src, timestamp);
        }
        return;
    }

    if (!other)
        other
            = find_controlled_window<win::x11::window>(win->space, predicate_match::window, above);

    if (other && detail == XCB_STACK_MODE_ABOVE) {
        auto it = win->space.stacking_order->sorted().cend();
        auto begin = win->space.stacking_order->sorted().cbegin();

        while (--it != begin) {
            if (*it == other) {
                // the other one is top on stack
                // invalidate and force
                it = begin;
                src = NET::FromTool;
                break;
            }
            auto c = qobject_cast<Win*>(*it);

            if (!c
                || !(is_normal(*it) && c->isShown() && (*it)->isOnCurrentDesktop()
                     && on_screen(*it, win->central_output))) {
                continue;
            }

            if (*(it - 1) == other)
                break; // "it" is the one above the target one, stack below "it"
        }

        if (it != begin && (*(it - 1) == other)) {
            other = qobject_cast<Win*>(*it);
        } else {
            other = nullptr;
        }
    }

    if (other) {
        restack(&win->space, win, other);
    } else if (detail == XCB_STACK_MODE_BELOW) {
        lower_client_request(&win->space, win, src, timestamp);
    } else if (detail == XCB_STACK_MODE_ABOVE) {
        raise_client_request(&win->space, win, src, timestamp);
    }

    if (send_event) {
        send_synthetic_configure_notify(win, frame_to_client_rect(win, win->frameGeometry()));
    }
}

template<typename Win>
void update_allowed_actions(Win* win, bool force = false)
{
    if (!win->control && !force) {
        return;
    }

    auto old_allowed_actions = NET::Actions(win->allowed_actions);
    win->allowed_actions = NET::Actions();

    if (win->isMovable()) {
        win->allowed_actions |= NET::ActionMove;
    }
    if (win->isResizable()) {
        win->allowed_actions |= NET::ActionResize;
    }
    if (win->isMinimizable()) {
        win->allowed_actions |= NET::ActionMinimize;
    }

    // Sticky state not supported
    if (win->isMaximizable()) {
        win->allowed_actions |= NET::ActionMax;
    }
    if (win->userCanSetFullScreen()) {
        win->allowed_actions |= NET::ActionFullScreen;
    }

    // Always (Pagers shouldn't show Docks etc.)
    win->allowed_actions |= NET::ActionChangeDesktop;

    if (win->isCloseable()) {
        win->allowed_actions |= NET::ActionClose;
    }
    if (old_allowed_actions == win->allowed_actions) {
        return;
    }

    // TODO: This could be delayed and compressed - It's only for pagers etc. anyway
    win->info->setAllowedActions(win->allowed_actions);

    // ONLY if relevant features have changed (and the window didn't just get/loose moveresize for
    // maximization state changes)
    auto const relevant = ~(NET::ActionMove | NET::ActionResize);

    if ((win->allowed_actions & relevant) != (old_allowed_actions & relevant)) {
        if ((win->allowed_actions & NET::ActionMinimize)
            != (old_allowed_actions & NET::ActionMinimize)) {
            Q_EMIT win->minimizeableChanged(win->allowed_actions & NET::ActionMinimize);
        }
        if ((win->allowed_actions & NET::ActionMax) != (old_allowed_actions & NET::ActionMax)) {
            Q_EMIT win->maximizeableChanged(win->allowed_actions & NET::ActionMax);
        }
    }
}

/**
 * Updates the user time (time of last action in the active window).
 * This is called inside  kwin for every action with the window
 * that qualifies for user interaction (clicking on it, activate it
 * externally, etc.).
 */
template<typename Win>
void update_user_time(Win* win, xcb_timestamp_t time = XCB_TIME_CURRENT_TIME)
{
    // copied in Group::updateUserTime
    if (time == XCB_TIME_CURRENT_TIME) {
        kwinApp()->update_x11_time_from_clock();
        time = xTime();
    }
    if (time != -1U
        && (win->user_time == XCB_TIME_CURRENT_TIME
            || NET::timestampCompare(time, win->user_time) > 0)) {
        // time > user_time
        win->user_time = time;
    }

    win->group()->updateUserTime(win->user_time);
}

template<typename Win>
xcb_timestamp_t read_user_creation_time(Win* win)
{
    base::x11::xcb::property prop(false,
                                  win->xcb_window(),
                                  win->space.atoms->kde_net_wm_user_creation_time,
                                  XCB_ATOM_CARDINAL,
                                  0,
                                  1);
    return prop.value<xcb_timestamp_t>(-1);
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
        auto act = dynamic_cast<Win*>(win->space.mostRecentlyActivatedClient());
        if (act != nullptr
            && !belong_to_same_application(act, win, same_client_check::relaxed_for_active)) {
            bool first_window = true;
            auto sameApplicationActiveHackPredicate = [win](Toplevel const* cl) {
                // ignore already existing splashes, toolbars, utilities and menus,
                // as the app may show those before the main window
                auto x11_client = qobject_cast<Win const*>(cl);
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
                for (auto win : win->space.m_windows) {
                    if (win->control && sameApplicationActiveHackPredicate(win)) {
                        first_window = false;
                        break;
                    }
                }
            }
            // don't refuse if focus stealing prevention is turned off
            if (!first_window
                && win->control->rules().checkFSP(
                       kwinApp()->options->focusStealingPreventionLevel())
                    > 0) {
                qCDebug(KWIN_CORE) << "User timestamp, already exists:" << 0;
                return 0; // refuse activation
            }
        }
        // Creation time would just mess things up during session startup,
        // as possibly many apps are started up at the same time.
        // If there's no active window yet, no timestamp will be needed,
        // as plain Workspace::allowClientActivation() will return true
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

template<typename Win>
xcb_timestamp_t user_time(Win* win)
{
    auto time = win->user_time;
    if (time == 0) {
        // doesn't want focus after showing
        return 0;
    }
    assert(win->group() != nullptr);
    if (time == -1U
        || (win->group()->userTime() != -1U
            && NET::timestampCompare(win->group()->userTime(), time) > 0)) {
        time = win->group()->userTime();
    }
    return time;
}

template<typename Win>
void startup_id_changed(Win* win)
{
    KStartupInfoId asn_id;
    KStartupInfoData asn_data;
    bool asn_valid = win->space.checkStartupNotification(win->xcb_window(), asn_id, asn_data);
    if (!asn_valid)
        return;
    // If the ASN contains desktop, move it to the desktop, otherwise move it to the current
    // desktop (since the new ASN should make the window act like if it's a new application
    // launched). However don't affect the window's desktop if it's set to be on all desktops.
    int desktop = win->space.virtual_desktop_manager->current();
    if (asn_data.desktop() != 0)
        desktop = asn_data.desktop();
    if (!win->isOnAllDesktops()) {
        win->space.sendClientToDesktop(win, desktop, true);
    }
    if (asn_data.xinerama() != -1) {
        auto output = base::get_output(kwinApp()->get_base().get_outputs(), asn_data.xinerama());
        if (output) {
            send_to_screen(win->space, win, *output);
        }
    }
    auto const timestamp = asn_id.timestamp();
    if (timestamp != 0) {
        auto activate = win->space.allowClientActivation(win, timestamp);
        if (asn_data.desktop() != 0 && !win->isOnCurrentDesktop()) {
            // it was started on different desktop than current one
            activate = false;
        }
        if (activate) {
            win->space.activateClient(win);
        } else {
            set_demands_attention(win, true);
        }
    }
}

template<typename Win>
void update_urgency(Win* win)
{
    if (win->info->urgency()) {
        set_demands_attention(win, true);
    }
}

template<typename Win>
base::x11::xcb::property fetch_first_in_tabbox(Win* win)
{
    auto& atoms = win->space.atoms;
    return base::x11::xcb::property(false,
                                    win->xcb_windows.client,
                                    atoms->kde_first_in_window_list,
                                    atoms->kde_first_in_window_list,
                                    0,
                                    1);
}

template<typename Win>
void read_first_in_tabbox(Win* win, base::x11::xcb::property& property)
{
    win->control->set_first_in_tabbox(
        property.to_bool(32, win->space.atoms->kde_first_in_window_list));
}

template<typename Win>
void update_first_in_tabbox(Win* win)
{
    // TODO: move into KWindowInfo
    auto property = fetch_first_in_tabbox(win);
    read_first_in_tabbox(win, property);
}

template<typename Win>
void cancel_focus_out_timer(Win* win)
{
    if (win->focus_out_timer) {
        win->focus_out_timer->stop();
    }
}

template<typename Win>
base::x11::xcb::property fetch_show_on_screen_edge(Win* win)
{
    return base::x11::xcb::property(
        false, win->xcb_window(), win->space.atoms->kde_screen_edge_show, XCB_ATOM_CARDINAL, 0, 1);
}

template<typename Win>
void read_show_on_screen_edge(Win* win, base::x11::xcb::property& property)
{
    // value comes in two parts, edge in the lower byte
    // then the type in the upper byte
    // 0 = autohide
    // 1 = raise in front on activate

    auto const value = property.value<uint32_t>(ElectricNone);
    auto border = ElectricNone;

    switch (value & 0xFF) {
    case 0:
        border = ElectricTop;
        break;
    case 1:
        border = ElectricRight;
        break;
    case 2:
        border = ElectricBottom;
        break;
    case 3:
        border = ElectricLeft;
        break;
    }

    if (border != ElectricNone) {
        QObject::disconnect(win->connections.edge_remove);
        QObject::disconnect(win->connections.edge_geometry);
        auto successfullyHidden = false;

        if (((value >> 8) & 0xFF) == 1) {
            set_keep_below(win, true);

            // request could have failed due to user kwin rules
            successfullyHidden = win->control->keep_below();

            win->connections.edge_remove
                = QObject::connect(win, &Win::keepBelowChanged, win, [win]() {
                      if (!win->control->keep_below()) {
                          win->space.edges->reserve(win, ElectricNone);
                      }
                  });
        } else {
            win->hideClient(true);
            successfullyHidden = win->isHiddenInternal();

            win->connections.edge_geometry
                = QObject::connect(win, &Win::frame_geometry_changed, win, [win, border]() {
                      win->hideClient(true);
                      win->space.edges->reserve(win, border);
                  });
        }

        if (successfullyHidden) {
            win->space.edges->reserve(win, border);
        } else {
            win->space.edges->reserve(win, ElectricNone);
        }
    } else if (!property.is_null() && property->type != XCB_ATOM_NONE) {
        // property value is incorrect, delete the property
        // so that the client knows that it is not hidden
        xcb_delete_property(
            connection(), win->xcb_window(), win->space.atoms->kde_screen_edge_show);
    } else {
        // restore
        // TODO: add proper unreserve

        // this will call showOnScreenEdge to reset the state
        QObject::disconnect(win->connections.edge_geometry);
        win->space.edges->reserve(win, ElectricNone);
    }
}

template<typename Win>
void update_show_on_screen_edge(Win* win)
{
    auto property = fetch_show_on_screen_edge(win);
    read_show_on_screen_edge(win, property);
}

template<typename Win>
base::x11::xcb::string_property fetch_application_menu_service_name(Win* win)
{
    return base::x11::xcb::string_property(win->xcb_windows.client,
                                           win->space.atoms->kde_net_wm_appmenu_service_name);
}

template<typename Win>
void read_application_menu_service_name(Win* win, base::x11::xcb::string_property& property)
{
    auto const& [_, path] = win->control->application_menu();
    win->control->update_application_menu({QString::fromUtf8(property), path});
}

template<typename Win>
void check_application_menu_service_name(Win* win)
{
    auto property = fetch_application_menu_service_name(win);
    read_application_menu_service_name(win, property);
}

template<typename Win>
base::x11::xcb::string_property fetch_application_menu_object_path(Win* win)
{
    return base::x11::xcb::string_property(win->xcb_windows.client,
                                           win->space.atoms->kde_net_wm_appmenu_object_path);
}

template<typename Win>
void read_application_menu_object_path(Win* win, base::x11::xcb::string_property& property)
{
    auto const& [name, _] = win->control->application_menu();
    win->control->update_application_menu({name, QString::fromUtf8(property)});
}

template<typename Win>
void check_application_menu_object_path(Win* win)
{
    auto property = fetch_application_menu_object_path(win);
    read_application_menu_object_path(win, property);
}

}
