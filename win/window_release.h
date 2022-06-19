/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "remnant.h"
#include "space_helpers.h"
#include "toplevel.h"

namespace KWin::win
{

template<typename RemnantWin, typename Win>
RemnantWin* create_remnant(Win& source)
{
    if (!source.space.render.scene) {
        // Don't create effect remnants when we don't render.
        return nullptr;
    }
    if (!source.ready_for_painting) {
        // Don't create remnants for windows that have never been shown.
        return nullptr;
    }

    auto win = new RemnantWin(source.space);
    win->copyToDeleted(&source);
    win->remnant = std::make_unique<win::remnant>(win, &source);

    win::add_remnant(source, *win);
    Q_EMIT source.remnant_created(win);
    return win;
}

}
