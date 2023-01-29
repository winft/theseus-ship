/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

namespace KWin::render
{

// for delayed supportproperty management of effects
template<typename Compositor>
void keep_support_property(Compositor& comp, xcb_atom_t atom)
{
    comp.unused_support_properties.removeAll(atom);
}

template<typename Compositor>
void remove_support_property(Compositor& comp, xcb_atom_t atom)
{
    comp.unused_support_properties << atom;
    comp.unused_support_property_timer.start();
}

template<typename Compositor>
void delete_unused_support_properties(Compositor& comp)
{
    if (comp.state == state::starting || comp.state == state::stopping) {
        // Currently still maybe restarting the compositor.
        comp.unused_support_property_timer.start();
        return;
    }

    auto con = comp.platform.base.x11_data.connection;
    if (!con) {
        return;
    }

    for (auto const& atom : qAsConst(comp.unused_support_properties)) {
        // remove property from root window
        xcb_delete_property(con, comp.platform.base.x11_data.root_window, atom);
    }
    comp.unused_support_properties.clear();
}

}
