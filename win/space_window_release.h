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
    remove_all(space.stacking.order.pre_stack, win);
    remove_all(space.stacking.order.stack, win);
}

template<typename Space, typename Win>
void remove_window_from_lists(Space& space, Win* win)
{
    remove_all(space.windows, win);
    space.stacking.order.render_restack_required = true;
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

template<typename Win>
void space_add_remnant(Win& source, Win& remnant)
{
    auto& space = source.space;
    assert(!contains(space.windows, &remnant));

    space.windows.push_back(&remnant);

    auto const unconstraintedIndex = index_of(space.stacking.order.pre_stack, &source);
    if (unconstraintedIndex != -1) {
        space.stacking.order.pre_stack.at(unconstraintedIndex) = &remnant;
    } else {
        space.stacking.order.pre_stack.push_back(&remnant);
    }

    auto const index = index_of(space.stacking.order.stack, &source);
    if (index != -1) {
        space.stacking.order.stack.at(index) = &remnant;
    } else {
        space.stacking.order.stack.push_back(&remnant);
    }

    QObject::connect(remnant.qobject.get(),
                     &decltype(remnant.qobject)::element_type::needsRepaint,
                     space.base.render->compositor->qobject.get(),
                     [&] { remnant.space.base.render->compositor->schedule_repaint(&remnant); });

    Q_EMIT space.qobject->remnant_created(remnant.signal_id);
}

}
