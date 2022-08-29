/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "desktop_space.h"
#include "move.h"
#include "net.h"
#include "output_space.h"
#include "placement.h"
#include "stacking.h"
#include "window_operation.h"

namespace KWin::win
{

template<typename Space>
bool has_usable_active_window(Space& space)
{
    return space.active_client
        && !(is_desktop(space.active_client) || is_dock(space.active_client));
}

template<typename Space>
void active_window_to_desktop(Space& space, unsigned int i)
{
    if (has_usable_active_window(space)) {
        if (i < 1) {
            return;
        }

        if (i >= 1 && i <= space.virtual_desktop_manager->count())
            send_window_to_desktop(space, space.active_client, i, true);
    }
}

template<typename Space>
void active_window_to_output(Space& space, QAction* action)
{
    if (has_usable_active_window(space)) {
        int const screen = get_action_data_as_uint(action);
        auto output = base::get_output(space.base.outputs, screen);
        if (output) {
            send_to_screen(space, space.active_client, *output);
        }
    }
}

template<typename Space>
void active_window_to_next_output(Space& space)
{
    if (!has_usable_active_window(space)) {
        return;
    }
    if (auto output = get_derivated_output(space.base, space.active_client->central_output, 1)) {
        send_to_screen(space, space.active_client, *output);
    }
}

template<typename Space>
void active_window_to_prev_output(Space& space)
{
    if (!has_usable_active_window(space)) {
        return;
    }
    if (auto output = get_derivated_output(space.base, space.active_client->central_output, -1)) {
        send_to_screen(space, space.active_client, *output);
    }
}

template<typename Space>
void active_window_maximize(Space& space)
{
    if (has_usable_active_window(space)) {
        perform_window_operation(space.active_client, base::options_qobject::MaximizeOp);
    }
}

template<typename Space>
void active_window_maximize_vertical(Space& space)
{
    if (has_usable_active_window(space)) {
        perform_window_operation(space.active_client, base::options_qobject::VMaximizeOp);
    }
}

template<typename Space>
void active_window_maximize_horizontal(Space& space)
{
    if (has_usable_active_window(space)) {
        perform_window_operation(space.active_client, base::options_qobject::HMaximizeOp);
    }
}

template<typename Space>
void active_window_minimize(Space& space)
{
    if (has_usable_active_window(space)) {
        perform_window_operation(space.active_client, base::options_qobject::MinimizeOp);
    }
}

template<typename Space>
void active_window_raise(Space& space)
{
    if (has_usable_active_window(space)) {
        raise_window(&space, space.active_client);
    }
}

template<typename Space>
void active_window_lower(Space& space)
{
    if (!has_usable_active_window(space)) {
        return;
    }

    lower_window(&space, space.active_client);
    // As this most likely makes the window no longer visible change the
    // keyboard focus to the next available window.
    // activateNextClient( c ); // Doesn't work when we lower a child window
    if (space.active_client->control->active
        && kwinApp()->options->qobject->focusPolicyIsReasonable()) {
        if (kwinApp()->options->qobject->isNextFocusPrefersMouse()) {
            auto next = window_under_mouse(space, space.active_client->central_output);
            if (next && next != space.active_client)
                request_focus(space, next);
        } else {
            activate_window(
                space,
                top_client_on_desktop(&space, space.virtual_desktop_manager->current(), nullptr));
        }
    }
}

template<typename Space>
void active_window_raise_or_lower(Space& space)
{
    if (has_usable_active_window(space)) {
        raise_or_lower_client(&space, space.active_client);
    }
}

template<typename Space>
void active_window_set_on_all_desktops(Space& space)
{
    if (has_usable_active_window(space)) {
        set_on_all_desktops(space.active_client, !space.active_client->isOnAllDesktops());
    }
}

template<typename Space>
void active_window_set_fullscreen(Space& space)
{
    if (has_usable_active_window(space)) {
        perform_window_operation(space.active_client, base::options_qobject::FullScreenOp);
    }
}

template<typename Space>
void active_window_set_no_border(Space& space)
{
    if (has_usable_active_window(space)) {
        perform_window_operation(space.active_client, base::options_qobject::NoBorderOp);
    }
}

template<typename Space>
void active_window_set_keep_above(Space& space)
{
    if (has_usable_active_window(space)) {
        perform_window_operation(space.active_client, base::options_qobject::KeepAboveOp);
    }
}

template<typename Space>
void active_window_set_keep_below(Space& space)
{
    if (has_usable_active_window(space)) {
        perform_window_operation(space.active_client, base::options_qobject::KeepBelowOp);
    }
}

template<typename Space>
void active_window_setup_window_shortcut(Space& space)
{
    if (has_usable_active_window(space)) {
        perform_window_operation(space.active_client, base::options_qobject::SetupWindowShortcutOp);
    }
}

template<typename Space>
void active_window_close(Space& space)
{
    if (has_usable_active_window(space)) {
        perform_window_operation(space.active_client, base::options_qobject::CloseOp);
    }
}

template<typename Space>
void active_window_move(Space& space)
{
    if (has_usable_active_window(space)) {
        perform_window_operation(space.active_client, base::options_qobject::UnrestrictedMoveOp);
    }
}

template<typename Space>
void active_window_resize(Space& space)
{
    if (has_usable_active_window(space)) {
        perform_window_operation(space.active_client, base::options_qobject::UnrestrictedResizeOp);
    }
}

template<typename Space>
void active_window_increase_opacity(Space& space)
{
    if (space.active_client) {
        space.active_client->setOpacity(qMin(space.active_client->opacity() + 0.05, 1.0));
    }
}

template<typename Space>
void active_window_lower_opacity(Space& space)
{
    if (space.active_client) {
        space.active_client->setOpacity(qMax(space.active_client->opacity() - 0.05, 0.05));
    }
}

template<typename Space>
void active_window_to_next_desktop(Space& space)
{
    if (has_usable_active_window(space)) {
        window_to_next_desktop(*space.active_client);
    }
}

template<typename Space>
void active_window_to_prev_desktop(Space& space)
{
    if (has_usable_active_window(space)) {
        window_to_prev_desktop(*space.active_client);
    }
}

template<typename Direction, typename Space>
void active_window_to_desktop(Space& space)
{
    auto& vds = space.virtual_desktop_manager;
    int const current = vds->current();
    Direction functor(*vds);

    int const d = functor(current, kwinApp()->options->qobject->isRollOverDesktops());
    if (d == current) {
        return;
    }

    set_move_resize_window(space, space.active_client);
    vds->setCurrent(d);
    set_move_resize_window(space, nullptr);
}

template<typename Space>
void active_window_to_right_desktop(Space& space)
{
    if (has_usable_active_window(space)) {
        active_window_to_desktop<virtual_desktop_right>(space);
    }
}

template<typename Space>
void active_window_to_left_desktop(Space& space)
{
    if (has_usable_active_window(space)) {
        active_window_to_desktop<virtual_desktop_left>(space);
    }
}

template<typename Space>
void active_window_to_above_desktop(Space& space)
{
    if (has_usable_active_window(space)) {
        active_window_to_desktop<virtual_desktop_above>(space);
    }
}

template<typename Space>
void active_window_to_below_desktop(Space& space)
{
    if (has_usable_active_window(space)) {
        active_window_to_desktop<virtual_desktop_below>(space);
    }
}

template<typename Space>
void active_window_show_operations_popup(Space& space)
{
    if (!space.active_client) {
        return;
    }

    auto pos = frame_to_client_pos(space.active_client, space.active_client->pos());
    space.user_actions_menu->show(QRect(pos, pos), space.active_client);
}

template<typename Space>
void active_window_pack_left(Space& space)
{
    if (!can_move(space.active_client)) {
        return;
    }
    auto const pos = space.active_client->geometry_update.frame.topLeft();
    pack_to(space.active_client,
            get_pack_position_left(space, space.active_client, pos.x(), true),
            pos.y());
}

template<typename Space>
void active_window_pack_right(Space& space)
{
    if (!can_move(space.active_client)) {
        return;
    }

    auto const pos = space.active_client->geometry_update.frame.topLeft();
    auto const width = space.active_client->geometry_update.frame.size().width();
    pack_to(space.active_client,
            get_pack_position_right(space, space.active_client, pos.x() + width, true) - width + 1,
            pos.y());
}

template<typename Space>
void active_window_pack_up(Space& space)
{
    if (!can_move(space.active_client)) {
        return;
    }

    auto const pos = space.active_client->geometry_update.frame.topLeft();
    pack_to(space.active_client,
            pos.x(),
            get_pack_position_up(space, space.active_client, pos.y(), true));
}

template<typename Space>
void active_window_pack_down(Space& space)
{
    if (!can_move(space.active_client)) {
        return;
    }

    auto const pos = space.active_client->geometry_update.frame.topLeft();
    auto const height = space.active_client->geometry_update.frame.size().height();
    pack_to(space.active_client,
            pos.x(),
            get_pack_position_down(space, space.active_client, pos.y() + height, true) - height
                + 1);
}

template<typename Space>
void active_window_grow_horizontal(Space& space)
{
    if (space.active_client) {
        grow_horizontal(space.active_client);
    }
}

template<typename Space>
void active_window_shrink_horizontal(Space& space)
{
    if (space.active_client) {
        shrink_horizontal(space.active_client);
    }
}

template<typename Space>
void active_window_grow_vertical(Space& space)
{
    if (space.active_client) {
        grow_vertical(space.active_client);
    }
}

template<typename Space>
void active_window_shrink_vertical(Space& space)
{
    if (space.active_client) {
        shrink_vertical(space.active_client);
    }
}

template<typename Space>
void active_window_quicktile(Space& space, quicktiles mode)
{
    if (!space.active_client) {
        return;
    }

    // If the user invokes two of these commands in a one second period, try to
    // combine them together to enable easy and intuitive corner tiling
    if (!space.m_quickTileCombineTimer->isActive()) {
        space.m_quickTileCombineTimer->start(1000);
        space.m_lastTilingMode = mode;
    } else {
        auto const was_left_or_right = space.m_lastTilingMode == quicktiles::left
            || space.m_lastTilingMode == quicktiles::right;
        auto const was_top_or_bottom = space.m_lastTilingMode == quicktiles::top
            || space.m_lastTilingMode == quicktiles::bottom;

        auto const is_left_or_right = mode == quicktiles::left || mode == quicktiles::right;
        auto const is_top_or_bottom = mode == quicktiles::top || mode == quicktiles::bottom;

        if ((was_left_or_right && is_top_or_bottom) || (was_top_or_bottom && is_left_or_right)) {
            mode |= space.m_lastTilingMode;
        }
        space.m_quickTileCombineTimer->stop();
    }

    set_quicktile_mode(space.active_client, mode, true);
}

}
