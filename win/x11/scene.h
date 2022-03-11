/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "hide.h"

#include "win/scene.h"

namespace KWin::win::x11
{

template<typename Win>
auto setup_compositing(Win& win, bool add_full_damage)
{
    static_assert(!Win::is_toplevel);

    if (!win::setup_compositing(win, add_full_damage)) {
        return false;
    }

    if (win.control) {
        // for internalKeep()
        update_visibility(&win);
    }

    return true;
}

template<typename Win>
void update_window_buffer(Win* win)
{
    if (win->render) {
        win->render->update_buffer();
    }
}

}
