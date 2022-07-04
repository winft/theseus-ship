/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "activation.h"
#include "screen.h"
#include "stacking.h"

#include "base/output_helpers.h"
#include "main.h"

namespace KWin::win
{

template<typename Space>
void set_current_output(Space& space, base::output const& output)
{
    if (!kwinApp()->options->focusPolicyIsReasonable()) {
        return;
    }

    close_active_popup(space);

    int const desktop = space.virtual_desktop_manager->current();
    auto get_focus = focus_chain_get_for_activation<Toplevel>(space.focus_chain, desktop, &output);

    if (get_focus == nullptr) {
        get_focus = find_desktop(&space, true, desktop);
    }

    if (get_focus != nullptr && get_focus != most_recently_activated_window(space)) {
        request_focus(space, get_focus);
    }

    base::set_current_output(kwinApp()->get_base(), &output);
}

}
