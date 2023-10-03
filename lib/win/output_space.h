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

#include <KProcess>

namespace KWin::win
{

template<typename Space>
inline bool is_output_switch_impossible(Space const& space)
{
    if (!space.options->get_current_output_follows_mouse()) {
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
    if (!space.options->qobject->focusPolicyIsReasonable()) {
        return;
    }

    close_active_popup(space);

    int const desktop = space.subspace_manager->current();
    auto get_focus = focus_chain_get_for_activation(space, desktop, &output);

    if (!get_focus) {
        get_focus = find_desktop(&space, true, desktop);
    }

    if (get_focus && get_focus != most_recently_activated_window(space)) {
        std::visit(overload{[&](auto&& win) { request_focus(space, *win); }}, *get_focus);
    }

    base::set_current_output(space.base, &output);
}

template<typename Space>
void switch_to_output(Space& space, QAction* action)
{
    if (is_output_switch_impossible(space)) {
        return;
    }

    int const screen = get_action_data_as_uint(action);
    auto output = base::get_output(space.base.outputs, screen);

    if (output) {
        set_current_output(space, *output);
    }
}

template<typename Base>
typename Base::output_t const*
get_derivated_output(Base const& base, typename Base::output_t const* output, int drift)
{
    auto index = output ? base::get_output_index(base.outputs, *output) : 0;
    index += drift;
    return base::get_output(base.outputs, index % base.outputs.size());
}

template<typename Space>
typename Space::base_t::output_t const* get_derivated_output_from_current(Space& space, int drift)
{
    return get_derivated_output(space.base, get_current_output(space), drift);
}

template<typename Space>
void switch_to_next_output(Space& space)
{
    if (is_output_switch_impossible(space)) {
        return;
    }
    if (auto output = get_derivated_output_from_current(space, 1)) {
        set_current_output(space, *output);
    }
}

template<typename Space>
void switch_to_prev_output(Space& space)
{
    if (is_output_switch_impossible(space)) {
        return;
    }
    if (auto output = get_derivated_output_from_current(space, -1)) {
        set_current_output(space, *output);
    }
}

}
