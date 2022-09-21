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
    auto win = space.stacking.active;
    if (!win) {
        return false;
    }
    return std::visit(overload{[&](auto&& win) { return !is_desktop(win) && !is_dock(win); }},
                      *win);
}

template<typename Space>
void active_window_to_desktop(Space& space, unsigned int i)
{
    if (!has_usable_active_window(space)) {
        return;
    }
    if (i < 1 || i > space.virtual_desktop_manager->count()) {
        return;
    }

    std::visit(overload{[&](auto&& win) { send_window_to_desktop(space, win, i, true); }},
               *space.stacking.active);
}

template<typename Space>
void active_window_to_output(Space& space, QAction* action)
{
    if (!has_usable_active_window(space)) {
        return;
    }

    int const screen = get_action_data_as_uint(action);
    auto output = base::get_output(space.base.outputs, screen);
    if (!output) {
        return;
    }

    std::visit(overload{[&](auto&& win) { send_to_screen(space, win, *output); }},
               *space.stacking.active);
}

template<typename Space>
void active_window_to_next_output(Space& space)
{
    if (!has_usable_active_window(space)) {
        return;
    }
    std::visit(overload{[&](auto&& win) {
                   if (auto output
                       = get_derivated_output(space.base, win->topo.central_output, 1)) {
                       send_to_screen(space, win, *output);
                   }
               }},
               *space.stacking.active);
}

template<typename Space>
void active_window_to_prev_output(Space& space)
{
    if (!has_usable_active_window(space)) {
        return;
    }
    std::visit(overload{[&](auto&& win) {
                   if (auto output
                       = get_derivated_output(space.base, win->topo.central_output, -1)) {
                       send_to_screen(space, win, *output);
                   }
               }},
               *space.stacking.active);
}

template<typename Space>
void active_window_maximize(Space& space)
{
    if (has_usable_active_window(space)) {
        std::visit(overload{[&](auto&& win) {
                       perform_window_operation(win, base::options_qobject::MaximizeOp);
                   }},
                   *space.stacking.active);
    }
}

template<typename Space>
void active_window_maximize_vertical(Space& space)
{
    if (has_usable_active_window(space)) {
        std::visit(overload{[&](auto&& win) {
                       perform_window_operation(win, base::options_qobject::VMaximizeOp);
                   }},
                   *space.stacking.active);
    }
}

template<typename Space>
void active_window_maximize_horizontal(Space& space)
{
    if (has_usable_active_window(space)) {
        std::visit(overload{[&](auto&& win) {
                       perform_window_operation(win, base::options_qobject::HMaximizeOp);
                   }},
                   *space.stacking.active);
    }
}

template<typename Space>
void active_window_minimize(Space& space)
{
    if (has_usable_active_window(space)) {
        std::visit(overload{[&](auto&& win) {
                       perform_window_operation(win, base::options_qobject::MinimizeOp);
                   }},
                   *space.stacking.active);
    }
}

template<typename Space>
void active_window_raise(Space& space)
{
    if (has_usable_active_window(space)) {
        std::visit(overload{[&](auto&& win) { raise_window(space, win); }}, *space.stacking.active);
    }
}

template<typename Space>
void active_window_lower(Space& space)
{
    using var_win = typename Space::window_t;

    if (!has_usable_active_window(space)) {
        return;
    }

    std::visit(
        overload{[&](auto&& act_win) {
            lower_window(space, act_win);

            // As this most likely makes the window no longer visible change the
            // keyboard focus to the next available window.
            if (!act_win->control->active
                || !kwinApp()->options->qobject->focusPolicyIsReasonable()) {
                return;
            }

            if (kwinApp()->options->qobject->isNextFocusPrefersMouse()) {
                auto next = window_under_mouse(space, act_win->topo.central_output);
                if (next && *next != var_win(act_win)) {
                    std::visit(overload{[&](auto&& next) { request_focus(space, *next); }}, *next);
                }
                return;
            }

            if (auto top
                = top_client_on_desktop(space, space.virtual_desktop_manager->current(), nullptr)) {
                std::visit(overload{[&](auto&& top) { activate_window(space, *top); }}, *top);
                return;
            }

            deactivate_window(space);
        }},
        *space.stacking.active);
}

template<typename Space>
void active_window_raise_or_lower(Space& space)
{
    if (has_usable_active_window(space)) {
        std::visit(overload{[&](auto&& win) { raise_or_lower_client(space, win); }},
                   *space.stacking.active);
    }
}

template<typename Space>
void active_window_set_on_all_desktops(Space& space)
{
    if (has_usable_active_window(space)) {
        std::visit(overload{[&](auto&& win) { set_on_all_desktops(win, !on_all_desktops(win)); }},
                   *space.stacking.active);
    }
}

template<typename Space>
void active_window_set_fullscreen(Space& space)
{
    if (has_usable_active_window(space)) {
        std::visit(overload{[&](auto&& win) {
                       perform_window_operation(win, base::options_qobject::FullScreenOp);
                   }},
                   *space.stacking.active);
    }
}

template<typename Space>
void active_window_set_no_border(Space& space)
{
    if (has_usable_active_window(space)) {
        std::visit(overload{[&](auto&& win) {
                       perform_window_operation(win, base::options_qobject::NoBorderOp);
                   }},
                   *space.stacking.active);
    }
}

template<typename Space>
void active_window_set_keep_above(Space& space)
{
    if (has_usable_active_window(space)) {
        std::visit(overload{[&](auto&& win) {
                       perform_window_operation(win, base::options_qobject::KeepAboveOp);
                   }},
                   *space.stacking.active);
    }
}

template<typename Space>
void active_window_set_keep_below(Space& space)
{
    if (has_usable_active_window(space)) {
        std::visit(overload{[&](auto&& win) {
                       perform_window_operation(win, base::options_qobject::KeepBelowOp);
                   }},
                   *space.stacking.active);
    }
}

template<typename Space>
void active_window_setup_window_shortcut(Space& space)
{
    if (has_usable_active_window(space)) {
        std::visit(overload{[&](auto&& win) {
                       perform_window_operation(win, base::options_qobject::SetupWindowShortcutOp);
                   }},
                   *space.stacking.active);
    }
}

template<typename Space>
void active_window_close(Space& space)
{
    if (has_usable_active_window(space)) {
        std::visit(overload{[&](auto&& win) {
                       perform_window_operation(win, base::options_qobject::CloseOp);
                   }},
                   *space.stacking.active);
    }
}

template<typename Space>
void active_window_move(Space& space)
{
    if (has_usable_active_window(space)) {
        std::visit(overload{[&](auto&& win) {
                       perform_window_operation(win, base::options_qobject::UnrestrictedMoveOp);
                   }},
                   *space.stacking.active);
    }
}

template<typename Space>
void active_window_resize(Space& space)
{
    if (has_usable_active_window(space)) {
        std::visit(overload{[&](auto&& win) {
                       perform_window_operation(win, base::options_qobject::UnrestrictedResizeOp);
                   }},
                   *space.stacking.active);
    }
}

template<typename Space>
void active_window_increase_opacity(Space& space)
{
    if (auto win = space.stacking.active) {
        std::visit(overload{[&](auto&& win) { win->setOpacity(qMin(win->opacity() + 0.05, 1.0)); }},
                   *win);
    }
}

template<typename Space>
void active_window_lower_opacity(Space& space)
{
    if (auto win = space.stacking.active) {
        std::visit(
            overload{[&](auto&& win) { win->setOpacity(qMax(win->opacity() - 0.05, 0.05)); }},
            *win);
    }
}

template<typename Space>
void active_window_to_next_desktop(Space& space)
{
    if (has_usable_active_window(space)) {
        std::visit(overload{[&](auto&& win) { window_to_next_desktop(*win); }},
                   *space.stacking.active);
    }
}

template<typename Space>
void active_window_to_prev_desktop(Space& space)
{
    if (has_usable_active_window(space)) {
        std::visit(overload{[&](auto&& win) { window_to_prev_desktop(*win); }},
                   *space.stacking.active);
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

    assert(space.stacking.active);
    std::visit(overload{[&](auto&& win) { set_move_resize_window(space, *win); }},
               *space.stacking.active);
    vds->setCurrent(d);
    unset_move_resize_window(space);
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
    auto win = space.stacking.active;
    if (!win) {
        return;
    }

    std::visit(overload{[&](auto&& win) {
                   auto pos = frame_to_client_pos(win, win->geo.pos());
                   space.user_actions_menu->show(QRect(pos, pos), win);
               }},
               *win);
}

template<typename Space>
void active_window_pack_left(Space& space)
{
    auto win = space.stacking.active;
    if (!win) {
        return;
    }

    std::visit(overload{[&](auto&& win) {
                   if (!can_move(win)) {
                       return;
                   }
                   auto const pos = win->geo.update.frame.topLeft();
                   pack_to(win, get_pack_position_left(space, win, pos.x(), true), pos.y());
               }},
               *win);
}

template<typename Space>
void active_window_pack_right(Space& space)
{
    auto win = space.stacking.active;
    if (!win) {
        return;
    }

    std::visit(overload{[&](auto&& win) {
                   if (!can_move(win)) {
                       return;
                   }

                   auto const pos = win->geo.update.frame.topLeft();
                   auto const width = win->geo.update.frame.size().width();
                   pack_to(win,
                           get_pack_position_right(space, win, pos.x() + width, true) - width + 1,
                           pos.y());
               }},
               *win);
}

template<typename Space>
void active_window_pack_up(Space& space)
{
    auto win = space.stacking.active;
    if (!win) {
        return;
    }

    std::visit(overload{[&](auto&& win) {
                   if (!can_move(win)) {
                       return;
                   }

                   auto const pos = win->geo.update.frame.topLeft();
                   pack_to(win, pos.x(), get_pack_position_up(space, win, pos.y(), true));
               }},
               *win);
}

template<typename Space>
void active_window_pack_down(Space& space)
{
    auto win = space.stacking.active;
    if (!win) {
        return;
    }

    std::visit(overload{[&](auto&& win) {
                   if (!can_move(win)) {
                       return;
                   }

                   auto const pos = win->geo.update.frame.topLeft();
                   auto const height = win->geo.update.frame.size().height();
                   pack_to(win,
                           pos.x(),
                           get_pack_position_down(space, win, pos.y() + height, true) - height + 1);
               }},
               *win);
}

template<typename Space>
void active_window_grow_horizontal(Space& space)
{
    if (auto win = space.stacking.active) {
        std::visit(overload{[&](auto&& win) { grow_horizontal(win); }}, *win);
    }
}

template<typename Space>
void active_window_shrink_horizontal(Space& space)
{
    if (auto win = space.stacking.active) {
        std::visit(overload{[&](auto&& win) { shrink_horizontal(win); }}, *win);
    }
}

template<typename Space>
void active_window_grow_vertical(Space& space)
{
    if (auto win = space.stacking.active) {
        std::visit(overload{[&](auto&& win) { grow_vertical(win); }}, *win);
    }
}

template<typename Space>
void active_window_shrink_vertical(Space& space)
{
    if (auto win = space.stacking.active) {
        std::visit(overload{[&](auto&& win) { shrink_vertical(win); }}, *win);
    }
}

template<typename Space>
void active_window_quicktile(Space& space, quicktiles mode)
{
    if (!space.stacking.active) {
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

    std::visit(overload{[&](auto&& win) { set_quicktile_mode(win, mode, true); }},
               *space.stacking.active);
}

}
