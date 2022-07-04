/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <NETWM>

namespace KWin::win
{

template<typename Win>
bool is_desktop(Win* win)
{
    return win->windowType() == NET::Desktop;
}

template<typename Win>
bool is_dock(Win* win)
{
    return win->windowType() == NET::Dock;
}

template<typename Win>
bool is_menu(Win* win)
{
    return win->windowType() == NET::Menu;
}

template<typename Win>
bool is_toolbar(Win* win)
{
    return win->windowType() == NET::Toolbar;
}

template<typename Win>
bool is_splash(Win* win)
{
    return win->windowType() == NET::Splash;
}

template<typename Win>
bool is_utility(Win* win)
{
    return win->windowType() == NET::Utility;
}

template<typename Win>
bool is_dialog(Win* win)
{
    return win->windowType() == NET::Dialog;
}

template<typename Win>
bool is_normal(Win* win)
{
    return win->windowType() == NET::Normal;
}

template<typename Win>
bool is_dropdown_menu(Win* win)
{
    return win->windowType() == NET::DropdownMenu;
}

template<typename Win>
bool is_popup(Win* win)
{
    switch (win->windowType()) {
    case NET::ComboBox:
    case NET::DropdownMenu:
    case NET::PopupMenu:
    case NET::Tooltip:
        return true;
    default:
        return win->is_popup_end();
    }
}

template<typename Win>
bool is_popup_menu(Win* win)
{
    return win->windowType() == NET::PopupMenu;
}

template<typename Win>
bool is_tooltip(Win* win)
{
    return win->windowType() == NET::Tooltip;
}

template<typename Win>
bool is_notification(Win* win)
{
    return win->windowType() == NET::Notification;
}

template<typename Win>
bool is_critical_notification(Win* win)
{
    return win->windowType() == NET::CriticalNotification;
}

template<typename Win>
bool is_on_screen_display(Win* win)
{
    return win->windowType() == NET::OnScreenDisplay;
}

template<typename Win>
bool is_combo_box(Win* win)
{
    return win->windowType() == NET::ComboBox;
}

template<typename Win>
bool is_dnd_icon(Win* win)
{
    return win->windowType() == NET::DNDIcon;
}

template<typename Win>
bool wants_tab_focus(Win* win)
{
    auto const suitable_type = is_normal(win) || is_dialog(win);
    return suitable_type && win->wantsInput();
}

}
