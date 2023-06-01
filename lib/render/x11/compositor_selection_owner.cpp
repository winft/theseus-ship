/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "compositor_selection_owner.h"

namespace KWin::render::x11
{

compositor_selection_owner::compositor_selection_owner(char const* selection,
                                                       xcb_connection_t* con,
                                                       xcb_window_t root_window)
    : selection_owner(selection, con, root_window)
{
    connect(this, &compositor_selection_owner::lostOwnership, this, [this] { owning = false; });
}

bool compositor_selection_owner::is_owning() const
{
    return owning;
}

void compositor_selection_owner::own()
{
    if (owning) {
        return;
    }

    // Force claim ownership.
    claim(true);
    owning = true;
}

void compositor_selection_owner::disown()
{
    if (!owning) {
        return;
    }

    release();
    owning = false;
}

}
