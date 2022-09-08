/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/options.h"
#include "base/output_helpers.h"
#include "base/platform.h"
#include "main.h"

namespace KWin::win
{

template<typename Win>
bool on_screen(Win* win, base::output const* output)
{
    if (!output) {
        return false;
    }
    return output->geometry().intersects(win->frameGeometry());
}

/**
 * @brief Finds the best window to become the new active window in the focus chain for the given
 * virtual @p desktop.
 *
 * In case that separate output focus is used only windows on the current output are considered.
 * If no window for activation is found @c null is returned.
 *
 * @param desktop The virtual desktop to look for a window for activation
 * @return The window which could be activated or @c null if there is none.
 */
template<typename Space>
typename Space::base_t::output_t const* get_current_output(Space const& space)
{
    auto const& base = space.base;

    if (kwinApp()->options->get_current_output_follows_mouse()) {
        return base::get_nearest_output(base.outputs, space.input->platform.cursor->pos());
    }

    auto const cur = static_cast<typename Space::base_t::output_t const*>(base.topology.current);
    if (auto act_win = space.stacking.active; act_win && !win::on_screen(act_win, cur)) {
        return act_win->central_output;
    }
    return cur;
}

template<typename Base, typename Win>
void set_current_output_by_window(Base& base, Win const& window)
{
    if (!window.control->active) {
        return;
    }
    if (window.central_output && !win::on_screen(&window, base.topology.current)) {
        base::set_current_output(base, window.central_output);
    }
}

template<typename Win>
bool on_active_screen(Win* win)
{
    return on_screen(win, get_current_output(win->space));
}

}
