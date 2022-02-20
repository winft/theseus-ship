/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "osd.h"

#include "input/platform.h"
#include "main.h"
#include "toplevel.h"

#include <KLocalizedString>

namespace KWin::win
{

class kill_window
{
public:
    void start()
    {
        osd_show(
            i18n("Select window to force close with left click or enter.\nEscape or right click "
                 "to cancel."),
            QStringLiteral("window-close"));

        kwinApp()->input->start_interactive_window_selection(
            [](auto window) {
                osd_hide();

                if (!window) {
                    return;
                }

                if (window->control) {
                    window->killWindow();
                    return;
                }

                if (window->xcb_window()) {
                    xcb_kill_client(connection(), window->xcb_window());
                }
            },
            QByteArrayLiteral("pirate"));
    }
};

}
