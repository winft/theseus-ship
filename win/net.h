/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

namespace KWin::win
{

template<typename Win>
bool is_desktop(Win const* win)
{
    return win->windowType() == window_type::desktop;
}

template<typename Win>
bool is_dock(Win const* win)
{
    return win->windowType() == window_type::dock;
}

template<typename Win>
bool is_menu(Win const* win)
{
    return win->windowType() == window_type::menu;
}

template<typename Win>
bool is_toolbar(Win const* win)
{
    return win->windowType() == window_type::toolbar;
}

template<typename Win>
bool is_splash(Win const* win)
{
    return win->windowType() == window_type::splash;
}

template<typename Win>
bool is_utility(Win const* win)
{
    return win->windowType() == window_type::utility;
}

template<typename Win>
bool is_dialog(Win const* win)
{
    return win->windowType() == window_type::dialog;
}

template<typename Win>
bool is_normal(Win const* win)
{
    return win->windowType() == window_type::normal;
}

template<typename Win>
bool is_dropdown_menu(Win const* win)
{
    return win->windowType() == window_type::dropdown_menu;
}

template<typename Win>
bool is_popup(Win const* win)
{
    switch (win->windowType()) {
    case window_type::combo_box:
    case window_type::dropdown_menu:
    case window_type::popup_menu:
    case window_type::tooltip:
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
    return win->windowType() == window_type::popup_menu;
}

template<typename Win>
bool is_tooltip(Win const* win)
{
    return win->windowType() == window_type::tooltip;
}

template<typename Win>
bool is_notification(Win const* win)
{
    return win->windowType() == window_type::notification;
}

template<typename Win>
bool is_critical_notification(Win const* win)
{
    return win->windowType() == window_type::critical_notification;
}

template<typename Win>
bool is_applet_popup(Win const* win)
{
    return win->windowType() == window_type::applet_popup;
}

template<typename Win>
bool is_on_screen_display(Win const* win)
{
    return win->windowType() == window_type::on_screen_display;
}

template<typename Win>
bool is_combo_box(Win const* win)
{
    return win->windowType() == window_type::combo_box;
}

template<typename Win>
bool is_dnd_icon(Win const* win)
{
    return win->windowType() == window_type::dnd_icon;
}

template<typename Win>
bool wants_tab_focus(Win const* win)
{
    auto const suitable_type = is_normal(win) || is_dialog(win) || is_applet_popup(win);
    return suitable_type && win->wantsInput();
}

}
