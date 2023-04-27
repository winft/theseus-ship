/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

#include <QAction>

namespace KWin::win
{

static inline unsigned int get_action_data_as_uint(QAction* act)
{
    auto ok = false;
    unsigned int i = -1;

    if (act) {
        i = act->data().toUInt(&ok);
    }

    return ok ? i : -1;
}

/**
 * @brief Finds the window matching the condition expressed in @p func in @p list.
 *
 * @param list The list to search in.
 * @param func The condition function (compare std::find_if).
 * @return The found window or @c null if there is no matching window.
 */
template<class Win, class W>
Win* find_in_list(std::vector<Win*> const& list, std::function<bool(W const*)> func)
{
    static_assert(std::is_base_of<W, Win>::value, "W must be derived from Win");

    auto const it = std::find_if(list.cbegin(), list.cend(), func);
    if (it == list.cend()) {
        return nullptr;
    }
    return *it;
}

template<typename Win1, typename Win2>
bool belong_to_same_client(Win1 const* win1,
                           Win2 const* win2,
                           same_client_check checks = flags<same_client_check>())
{
    if constexpr (std::is_same_v<Win1, Win2>) {
        return win1->belongsToSameApplication(win2, checks);
    }
    return false;
}

}
