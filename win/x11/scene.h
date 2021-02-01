/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

namespace KWin::win::x11
{

template<typename Win>
void update_window_pixmap(Win* win)
{
    if (win->effectWindow() && win->effectWindow()->sceneWindow()) {
        win->effectWindow()->sceneWindow()->updatePixmap();
    }
}

}
