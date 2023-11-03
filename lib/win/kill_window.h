/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "osd.h"
#include <utils/algorithm.h>

#include <KLocalizedString>

namespace KWin::win
{

template<typename Space>
class kill_window
{
public:
    kill_window(Space& space)
        : space{space}
    {
    }

    void start()
    {
        osd_show(
            space,
            i18n("Select window to force close with left click or enter.\nEscape or right click "
                 "to cancel."),
            QStringLiteral("window-close"));

        space.input->start_interactive_window_selection(
            [this](auto window) {
                osd_hide(space);

                if (!window) {
                    return;
                }

                std::visit(
                    overload{[&](auto&& win) {
                        if (win->control) {
                            win->killWindow();
                            return;
                        }
                        if constexpr (requires(Space space) { space.base.x11_data.connection; }) {
                            if constexpr (requires(decltype(win) win) { win->xcb_windows; }) {
                                xcb_kill_client(space.base.x11_data.connection,
                                                win->xcb_windows.client);
                            }
                        }
                    }},
                    *window);
            },
            QByteArrayLiteral("pirate"));
    }

private:
    Space& space;
};

template<typename Space>
void start_window_killer(Space& space)
{
    if (!space.window_killer) {
        space.window_killer = std::make_unique<kill_window<Space>>(space);
    }
    space.window_killer->start();
}

}
