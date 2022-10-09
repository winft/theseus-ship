/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <NETWM>

namespace KWin::win
{

template<typename Win>
bool is_desktop(Win const* win)
{
    return win->windowType() == NET::Desktop;
}

template<typename Win>
bool is_dock(Win const* win)
{
    return win->windowType() == NET::Dock;
}

template<typename Win>
bool is_menu(Win const* win)
{
    return win->windowType() == NET::Menu;
}

template<typename Win>
bool is_toolbar(Win const* win)
{
    return win->windowType() == NET::Toolbar;
}

template<typename Win>
bool is_splash(Win const* win)
{
    return win->windowType() == NET::Splash;
}

template<typename Win>
bool is_utility(Win const* win)
{
    return win->windowType() == NET::Utility;
}

template<typename Win>
bool is_dialog(Win const* win)
{
    return win->windowType() == NET::Dialog;
}

template<typename Win>
bool is_normal(Win const* win)
{
    return win->windowType() == NET::Normal;
}

template<typename Win>
bool is_dropdown_menu(Win const* win)
{
    return win->windowType() == NET::DropdownMenu;
}

template<typename Win>
bool is_popup(Win const* win)
{
    switch (win->windowType()) {
    case NET::ComboBox:
    case NET::DropdownMenu:
    case NET::PopupMenu:
    case NET::Tooltip:
        return true;
    default:
        if constexpr (requires(Win win) { win.is_popup_end(); }) {
            return win->is_popup_end();
        } else {
            return win->remnant && win->remnant->data.was_popup_window;
        }
    }
}

template<typename Win>
bool is_popup_menu(Win const* win)
{
    return win->windowType() == NET::PopupMenu;
}

template<typename Win>
bool is_tooltip(Win const* win)
{
    return win->windowType() == NET::Tooltip;
}

template<typename Win>
bool is_notification(Win const* win)
{
    return win->windowType() == NET::Notification;
}

template<typename Win>
bool is_critical_notification(Win const* win)
{
    return win->windowType() == NET::CriticalNotification;
}

template<typename Win>
bool is_applet_popup(Win const* win)
{
    return win->windowType() == NET::AppletPopup;
}

template<typename Win>
bool is_on_screen_display(Win const* win)
{
    return win->windowType() == NET::OnScreenDisplay;
}

template<typename Win>
bool is_combo_box(Win const* win)
{
    return win->windowType() == NET::ComboBox;
}

template<typename Win>
bool is_dnd_icon(Win const* win)
{
    return win->windowType() == NET::DNDIcon;
}

template<typename Win>
bool wants_tab_focus(Win const* win)
{
    auto const suitable_type = is_normal(win) || is_dialog(win) || is_applet_popup(win);
    return suitable_type && win->wantsInput();
}

}
