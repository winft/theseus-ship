/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "utils/algorithm.h"

#include <cassert>

namespace KWin::win
{

template<typename Space, typename Win>
void remove_window_from_stacking_order(Space& space, Win* win)
{
    using var_win = typename Space::window_t;

    remove_all(space.stacking.order.pre_stack, var_win(win));
    remove_all(space.stacking.order.stack, var_win(win));
}

template<typename Space, typename Win>
void remove_window_from_lists(Space& space, Win* win)
{
    using var_win = typename Space::window_t;

    remove_all(space.windows, var_win(win));
    space.stacking.order.render_restack_required = true;
}

template<typename Space, typename Win>
void delete_window_from_space(Space& space, Win& win)
{
    remove_window_from_stacking_order(space, &win);
    remove_window_from_lists(space, &win);

    using compositor_t = typename Space::base_t::render_t::compositor_t;
    if constexpr (requires(compositor_t comp) { comp.update_blocking(nullptr); }) {
        space.base.render->compositor->update_blocking(nullptr);
    }

    Q_EMIT space.qobject->window_deleted(win.meta.signal_id);
    delete &win;
}

template<typename Win>
void space_add_remnant(Win& source, Win& remnant)
{
    using var_win = typename Win::space_t::window_t;

    auto& space = source.space;
    assert(!contains(space.windows, var_win(&remnant)));

    space.windows.push_back(&remnant);

    auto const unconstraintedIndex = index_of(space.stacking.order.pre_stack, var_win(&source));
    if (unconstraintedIndex != -1) {
        space.stacking.order.pre_stack.at(unconstraintedIndex) = &remnant;
    } else {
        space.stacking.order.pre_stack.push_back(&remnant);
    }

    auto const index = index_of(space.stacking.order.stack, var_win(&source));
    if (index != -1) {
        space.stacking.order.stack.at(index) = &remnant;
    } else {
        space.stacking.order.stack.push_back(&remnant);
    }

    QObject::connect(remnant.qobject.get(),
                     &decltype(remnant.qobject)::element_type::needsRepaint,
                     space.base.render->compositor->qobject.get(),
                     [&] { remnant.space.base.render->compositor->schedule_repaint(&remnant); });

    Q_EMIT space.qobject->remnant_created(remnant.meta.signal_id);
}

}
