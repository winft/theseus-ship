/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

namespace KWin::win
{

template<typename Win>
void block_geometry_updates(Win* win, bool block)
{
    if (block) {
        win->geo.update.block++;
        return;
    }

    win->geo.update.block--;

    if (!win->geo.update.block && win->geo.update.pending != pending_geometry::none) {
        win->setFrameGeometry(win->geo.update.frame);
    }
}

template<typename Win>
class geometry_updates_blocker
{
public:
    explicit geometry_updates_blocker(Win* c)
        : cl(c)
    {
        block_geometry_updates(cl, true);
    }
    ~geometry_updates_blocker()
    {
        block_geometry_updates(cl, false);
    }

private:
    Win* cl;
};

}
