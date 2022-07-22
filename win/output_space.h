/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "activation.h"
#include "screen.h"
#include "stacking.h"

#include "base/output.h"
#include "base/output_helpers.h"
#include "main.h"

#include <KProcess>

namespace KWin::win
{

static inline bool is_output_switch_impossible()
{
    if (!kwinApp()->options->get_current_output_follows_mouse()) {
        return false;
    }

    QStringList args;
    args << QStringLiteral("--passivepopup")
         << i18n(
                "The window manager is configured to consider the screen with the mouse on it as "
                "active one.\n"
                "Therefore it is not possible to switch to a screen explicitly.")
         << QStringLiteral("20");

    KProcess::startDetached(QStringLiteral("kdialog"), args);
    return true;
}

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

template<typename Space>
void switch_to_output(Space& space, QAction* action)
{
    if (is_output_switch_impossible()) {
        return;
    }

    int const screen = get_action_data_as_uint(action);
    auto output = base::get_output(kwinApp()->get_base().get_outputs(), screen);

    if (output) {
        set_current_output(space, *output);
    }
}

static inline base::output const* get_derivated_output(base::output const* output, int drift)
{
    auto const& outputs = kwinApp()->get_base().get_outputs();
    auto index = output ? base::get_output_index(outputs, *output) : 0;
    index += drift;
    return base::get_output(outputs, index % outputs.size());
}

template<typename Space>
base::output const* get_derivated_output(Space& space, int drift)
{
    return get_derivated_output(get_current_output(space), drift);
}

template<typename Space>
void switch_to_next_output(Space& space)
{
    if (is_output_switch_impossible()) {
        return;
    }
    if (auto output = get_derivated_output(space, 1)) {
        set_current_output(space, *output);
    }
}

template<typename Space>
void switch_to_prev_output(Space& space)
{
    if (is_output_switch_impossible()) {
        return;
    }
    if (auto output = get_derivated_output(space, -1)) {
        set_current_output(space, *output);
    }
}

}
