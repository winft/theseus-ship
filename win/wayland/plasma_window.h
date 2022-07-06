/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/activation.h"

#include <cassert>

#include <Wrapland/Server/plasma_window.h>

namespace KWin::win::wayland
{

template<typename Space>
void handle_change_showing_desktop(Space* space,
                                   Wrapland::Server::PlasmaWindowManager::ShowingDesktopState state)
{
    using State = Wrapland::Server::PlasmaWindowManager::ShowingDesktopState;

    if (!space) {
        return;
    }
    bool set = false;
    switch (state) {
    case State::Disabled:
        set = false;
        break;
    case State::Enabled:
        set = true;
        break;
    default:
        assert(false);
        break;
    }
    if (set == space->showingDesktop()) {
        return;
    }
    set_showing_desktop(*space, set);
}

}
