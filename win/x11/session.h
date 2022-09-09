/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "utils/algorithm.h"
#include "utils/blocker.h"

namespace KWin::win::x11
{

template<typename Space, typename Window>
void restore_session_stacking_order(Space space, Window* c)
{
    if (c->sm_stacking_order < 0) {
        return;
    }

    blocker block(space->stacking_order);
    remove_all(space->stacking_order.pre_stack, c);

    for (auto it = space->stacking_order.pre_stack.begin(); // from bottom
         it != space->stacking_order.pre_stack.end();
         ++it) {
        auto current = dynamic_cast<Window*>(*it);
        if (!current) {
            continue;
        }
        if (current->sm_stacking_order > c->sm_stacking_order) {
            space->stacking_order.pre_stack.insert(it, c);
            return;
        }
    }
    space->stacking_order.pre_stack.push_back(c);
}

}
