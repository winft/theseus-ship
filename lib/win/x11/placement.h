/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/xcb/property.h"
#include "utils/geo.h"
#include "win/geo.h"
#include "win/placement.h"
#include "win/session_manager.h"
#include "win/window_area.h"

namespace KWin::win::x11
{

/**
 * Checks if the window provides its own placement via geometry hint and we want to use it or if
 * this is overriden by us (via window rule).
 */
template<typename Win>
bool position_via_hint(Win* win, QRect const& geo, bool ignore_default, QRect& place_area)
{
    if (win->control->rules.checkIgnoreGeometry(ignore_default, true)) {
        // Hint is to be ignored via rule.
        return false;
    }
    if (!win->geometry_hints.has_position()) {
        return false;
    }

    // Window provides its own placement via geometry hint.

    // Disobey xinerama placement option for now (#70943)
    place_area
        = space_window_area(win->space, area_option::placement, geo.center(), get_desktop(*win));

    return true;
}

template<typename Win>
bool move_with_force_rule(Win* win, QRect& frame_geo, bool is_inital_placement, QRect& area)
{
    auto forced_pos = win->control->rules.checkPosition(geo::invalid_point, is_inital_placement);

    if (forced_pos == geo::invalid_point) {
        return false;
    }

    move(win, forced_pos);
    frame_geo = pending_frame_geometry(win);

    // Don't keep inside workarea if the window has specially configured position
    area = space_window_area(win->space, area_option::full, frame_geo.center(), get_desktop(*win));
    return true;
}

template<typename Win>
void resize_on_taking_control(Win* win, QRect& frame_geo, bool mapped)
{
    // TODO: Is CentralGravity right here, when resizing is done after gravitating?
    auto const adj_frame_size = adjusted_frame_size(win, frame_geo.size(), size_mode::any);
    auto const rule_checked_size = win->control->rules.checkSize(adj_frame_size, !mapped);
    win->setFrameGeometry(QRect(win->geo.pos(), rule_checked_size));
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
    if (win->geo.size().width() < area.width() && win->geo.size().height() < area.height()) {
        // Window smaller than the screen, do not maximize.
        frame_geo = keep_in_placement_area(win, area, partial_keep_in_area);
        return;
    }

    auto const screen_area
        = space_window_area(win->space, area_option::screen, area.center(), get_desktop(*win))
              .size();
    auto const full_area
        = space_window_area(win->space, area_option::full, frame_geo.center(), get_desktop(*win));
    auto const client_size = frame_to_client_size(win, win->geo.size());

    auto pseudo_max{maximize_mode::restore};

    if (win->net_info->state() & net::MaxVert) {
        pseudo_max |= maximize_mode::vertical;
    }
    if (win->net_info->state() & net::MaxHoriz) {
        pseudo_max |= maximize_mode::horizontal;
    }

    if (win->geo.size().width() >= area.width()) {
        pseudo_max |= maximize_mode::horizontal;
    }
    if (win->geo.size().height() >= area.height()) {
        pseudo_max |= maximize_mode::vertical;
    }

    // Heuristic: If a decorated client is smaller than the entire screen, the user might want to
    // move it around (multiscreen) in this case, if the decorated client is bigger than the screen
    // (+1), we don't take this as an attempt for maximization, but just constrain the size
    // (the window simply wants to be bigger).
    auto keep_in_fullscreen_area{false};

    if (win->geo.size().width() < full_area.width()
        && (client_size.width() > screen_area.width() + 1)) {
        pseudo_max = pseudo_max & ~maximize_mode::horizontal;
        keep_in_fullscreen_area = true;
    }
    if (win->geo.size().height() < full_area.height()
        && (client_size.height() > screen_area.height() + 1)) {
        pseudo_max = pseudo_max & ~maximize_mode::vertical;
        keep_in_fullscreen_area = true;
    }

    if (pseudo_max != maximize_mode::restore) {
        maximize(win, pseudo_max);
        assert(win->geo.update.max_mode == pseudo_max);

        // from now on, care about maxmode, since the maximization call will override mode
        // for fix aspects
        keep_in_area &= pseudo_max != maximize_mode::full;

        if (pseudo_max == maximize_mode::full) {
            // Unset restore geometry. On unmaximize we set to a default size and placement.
            win->geo.restore.max = QRect();
        } else if (flags(pseudo_max & maximize_mode::vertical)) {
            // Only vertically maximized. Restore horizontal axis only and choose some default
            // restoration for the vertical axis.
            assert(!(pseudo_max & maximize_mode::horizontal));
            auto restore_height = screen_area.height() * 2 / 3.;
            auto restore_y = (screen_area.height() - restore_height) / 2;
            win->geo.restore.max.setY(restore_y);
            win->geo.restore.max.setHeight(restore_height);
        } else {
            // Horizontally maximized only.
            assert(flags(pseudo_max & maximize_mode::horizontal));
            auto restore_width = screen_area.width() * 2 / 3.;
            auto restore_x = (screen_area.width() - restore_width) / 2;
            win->geo.restore.max.setX(restore_x);
            win->geo.restore.max.setWidth(restore_width);
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

    auto area
        = space_window_area(win->space, area_option::full, frame_geo.center(), get_desktop(*win));
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

    place_in_area(win, area);
    frame_geo = pending_frame_geometry(win);

    // The client may have been moved to another screen, update placement area.
    area = space_window_area(win->space, area_option::placement, win);

    place_max_fs(win, frame_geo, area, false, true);
    return area;
}

template<typename Win>
QRect place_session(Win* win, QRect& frame_geo)
{
    auto must_place{false};

    auto area
        = space_window_area(win->space, area_option::full, frame_geo.center(), get_desktop(*win));
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

    place_in_area(win, area);
    frame_geo = pending_frame_geometry(win);

    // The client may have been moved to another screen, update placement area.
    area = space_window_area(win->space, area_option::placement, win);
    frame_geo = keep_in_placement_area(win, area, true);
    return area;
}

template<typename Win>
bool ignore_position_default(Win* win)
{
    // TODO(romangg): This function flow can surely be radically simplified.
    if (win->transient->lead()) {
        if (!is_utility(win) && !is_dialog(win) && !is_splash(win)) {
            return false;
        }
        if (!win->net_info->hasNETSupport()) {
            return false;
        }
        // TODO(romangg): Should we return false here?
    }
    if (is_dialog(win) && win->net_info->hasNETSupport()) {
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
QRect place_unmapped(Win* win, QRect& frame_geo)
{
    QPoint center;
    auto output = get_current_output(win->space);

    if (output) {
        output = win->control->rules.checkScreen(win->space.base, output, true);
        center = output->geometry().center();
    }

    auto area = space_window_area(win->space, area_option::placement, center, get_desktop(*win));

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
        place_in_area(win, area);
        frame_geo = pending_frame_geometry(win);

        // The client may have been moved to another screen, update placement area.
        area = space_window_area(win->space, area_option::placement, win);
    }

    place_max_fs(win, frame_geo, area, false, false);

    return area;
}

// When kwin crashes, windows will not be gravitated back to their original position
// and will remain offset by the size of the decoration. So when restarting, fix this
// (the property with the size of the frame remains on the window after the crash).
template<typename Space>
void fix_position_after_crash(Space& space,
                              xcb_window_t w,
                              const xcb_get_geometry_reply_t* geometry)
{
    net::win_info i(space.base.x11_data.connection,
                    w,
                    space.base.x11_data.root_window,
                    net::WMFrameExtents,
                    net::Properties2());
    auto frame = i.frameExtents();

    if (frame.left != 0 || frame.top != 0) {
        // left and top needed due to narrowing conversations restrictions in C++11
        const uint32_t left = frame.left;
        const uint32_t top = frame.top;
        const uint32_t values[] = {geometry->x - left, geometry->y - top};
        xcb_configure_window(
            space.base.x11_data.connection, w, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
    }
}

}
