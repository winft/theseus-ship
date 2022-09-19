/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "window_qobject.h"

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
        return base::get_nearest_output(base.outputs, space.input->cursor->pos());
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

/**
 * Checks whether the screen number for this window changed and updates if needed. Any method
 * changing the geometry of the Toplevel should call this method.
 */
template<typename Win>
void check_screen(Win& win)
{
    auto const& outputs = win.space.base.outputs;
    auto output = base::get_nearest_output(outputs, win.frameGeometry().center());
    auto old_output = win.central_output;

    if (old_output == output) {
        return;
    }

    win.central_output = output;
    Q_EMIT win.qobject->central_output_changed(old_output, output);
}

template<typename Win>
void setup_check_screen(Win& win)
{
    win.notifiers.check_screen = QObject::connect(win.qobject.get(),
                                                  &window_qobject::frame_geometry_changed,
                                                  win.qobject.get(),
                                                  [&win] { check_screen(win); });
    check_screen(win);
}

template<typename Win, typename Output>
void handle_output_added(Win& win, Output* output)
{
    if (!win.central_output) {
        win.central_output = output;
        Q_EMIT win.qobject->central_output_changed(nullptr, output);
        return;
    }

    check_screen(win);
}

template<typename Win, typename Output>
void handle_output_removed(Win& win, Output* output)
{
    if (win.central_output != output) {
        return;
    }
    auto const& outputs = win.space.base.outputs;
    win.central_output = base::get_nearest_output(outputs, win.frameGeometry().center());
    Q_EMIT win.qobject->central_output_changed(output, win.central_output);
}

}
