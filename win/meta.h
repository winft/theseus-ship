/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control.h"

#include <klocalizedstring.h>

#include <QLatin1String>

namespace KWin::win
{

template<typename Win>
QString caption(Win* win)
{
    QString cap = win->captionNormal() + win->captionSuffix();
    if (win->control()->unresponsive()) {
        cap += QLatin1String(" ");
        cap += i18nc("Application is not responding, appended to window title", "(Not Responding)");
    }
    return cap;
}

template<typename Win>
QString shortcut_caption_suffix(Win* win)
{
    if (win->shortcut().isEmpty()) {
        return QString();
    }
    return QLatin1String(" {") + win->shortcut().toString() + QLatin1Char('}');
}

}
