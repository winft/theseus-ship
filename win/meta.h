/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <klocalizedstring.h>

#include <QLatin1String>

namespace KWin::win
{

template<typename Win>
QString caption(Win* win)
{
    QString cap = win->captionNormal() + win->captionSuffix();
    if (win->unresponsive()) {
        cap += QLatin1String(" ");
        cap += i18nc("Application is not responding, appended to window title", "(Not Responding)");
    }
    return cap;
}

}
