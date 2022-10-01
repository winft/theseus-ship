/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/shortcut_set.h"

namespace KWin::win::x11
{

template<typename Win>
void shortcut_set_internal(Win& win)
{
    win.updateCaption();
#if 0
    window_shortcut_updated(win.space, &win);
#else
    // Workaround for kwin<->kglobalaccel deadlock, when KWin has X grab and the kded
    // kglobalaccel module tries to create the key grab. KWin should preferably grab
    // they keys itself anyway :(.
    QTimer::singleShot(0, win.qobject.get(), [&win] { window_shortcut_updated(win.space, &win); });
#endif
}

}
