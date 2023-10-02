/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <win/focus_chain.h>
#include <win/stacking_order.h>

#include <deque>
#include <optional>

namespace KWin::win
{

template<typename Window>
struct stacking_state {
    win::stacking_order<Window> order;
    win::focus_chain<Window> focus_chain;

    // Last is most recent.
    std::deque<Window> should_get_focus;
    std::deque<Window> attention_chain;

    std::optional<Window> active;
    std::optional<Window> last_active;
    std::optional<Window> most_recently_raised;

    std::optional<Window> delayfocus_window;
};

}
