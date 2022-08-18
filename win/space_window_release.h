/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "utils/algorithm.h"

namespace KWin::win
{

template<typename Space, typename Win>
void remove_window_from_stacking_order(Space& space, Win* win)
{
    remove_all(space.stacking_order->pre_stack, win);
    remove_all(space.stacking_order->stack, win);
}

template<typename Space, typename Win>
void remove_window_from_lists(Space& space, Win* win)
{
    remove_all(space.windows, win);
    space.stacking_order->render_restack_required = true;
}

template<typename Space, typename Win>
void delete_window_from_space(Space& space, Win* win)
{
    remove_window_from_stacking_order(space, win);
    remove_window_from_lists(space, win);

    if (auto& update_block = space.base.render->compositor->x11_integration.update_blocking;
        update_block) {
        update_block(nullptr);
    }

    Q_EMIT space.qobject->window_deleted(win->signal_id);
    delete win;
}

template<typename Win1, typename Win2>
void add_remnant(Win1& orig, Win2& remnant)
{
    auto& space = orig.space;
    assert(!contains(space.windows, &remnant));

    space.windows.push_back(&remnant);

    auto const unconstraintedIndex = index_of(space.stacking_order->pre_stack, &orig);
    if (unconstraintedIndex != -1) {
        space.stacking_order->pre_stack.at(unconstraintedIndex) = &remnant;
    } else {
        space.stacking_order->pre_stack.push_back(&remnant);
    }

    auto const index = index_of(space.stacking_order->stack, &orig);
    if (index != -1) {
        space.stacking_order->stack.at(index) = &remnant;
    } else {
        space.stacking_order->stack.push_back(&remnant);
    }

    QObject::connect(remnant.qobject.get(),
                     &decltype(remnant.qobject)::element_type::needsRepaint,
                     space.base.render->compositor->qobject.get(),
                     [&] { remnant.space.base.render->compositor->schedule_repaint(&remnant); });
}

}
