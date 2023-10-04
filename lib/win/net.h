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
    return win->windowType() == win_type::desktop;
}

template<typename Win>
bool is_dock(Win const* win)
{
    return win->windowType() == win_type::dock;
}

template<typename Win>
bool is_menu(Win const* win)
{
    return win->windowType() == win_type::menu;
}

template<typename Win>
bool is_toolbar(Win const* win)
{
    return win->windowType() == win_type::toolbar;
}

template<typename Win>
bool is_splash(Win const* win)
{
    return win->windowType() == win_type::splash;
}

template<typename Win>
bool is_utility(Win const* win)
{
    return win->windowType() == win_type::utility;
}

template<typename Win>
bool is_dialog(Win const* win)
{
    return win->windowType() == win_type::dialog;
}

template<typename Win>
bool is_normal(Win const* win)
{
    return win->windowType() == win_type::normal;
}

template<typename Win>
bool is_dropdown_menu(Win const* win)
{
    return win->windowType() == win_type::dropdown_menu;
}

template<typename Win>
bool is_popup(Win const* win)
{
    switch (win->windowType()) {
    case win_type::combo_box:
    case win_type::dropdown_menu:
    case win_type::popup_menu:
    case win_type::tooltip:
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
    return win->windowType() == win_type::popup_menu;
}

template<typename Win>
bool is_tooltip(Win const* win)
{
    return win->windowType() == win_type::tooltip;
}

template<typename Win>
bool is_notification(Win const* win)
{
    return win->windowType() == win_type::notification;
}

template<typename Win>
bool is_critical_notification(Win const* win)
{
    return win->windowType() == win_type::critical_notification;
}

template<typename Win>
bool is_applet_popup(Win const* win)
{
    return win->windowType() == win_type::applet_popup;
}

template<typename Win>
bool is_on_screen_display(Win const* win)
{
    return win->windowType() == win_type::on_screen_display;
}

template<typename Win>
bool is_combo_box(Win const* win)
{
    return win->windowType() == win_type::combo_box;
}

template<typename Win>
bool is_dnd_icon(Win const* win)
{
    return win->windowType() == win_type::dnd_icon;
}

template<typename Win>
bool wants_tab_focus(Win const* win)
{
    auto const suitable_type = is_normal(win) || is_dialog(win) || is_applet_popup(win);
    return suitable_type && win->wantsInput();
}

inline bool type_matches_mask(win::win_type type, win::window_type_mask mask)
{
    switch (type) {
#define CHECK_TYPE_MASK(type)                                                                      \
    case win::win_type::type:                                                                      \
        if (flags(mask & win::window_type_mask::type))                                             \
            return true;                                                                           \
        break;
        CHECK_TYPE_MASK(normal)
        CHECK_TYPE_MASK(desktop)
        CHECK_TYPE_MASK(dock)
        CHECK_TYPE_MASK(toolbar)
        CHECK_TYPE_MASK(menu)
        CHECK_TYPE_MASK(dialog)
        CHECK_TYPE_MASK(override)
        CHECK_TYPE_MASK(top_menu)
        CHECK_TYPE_MASK(utility)
        CHECK_TYPE_MASK(splash)
        CHECK_TYPE_MASK(dropdown_menu)
        CHECK_TYPE_MASK(popup_menu)
        CHECK_TYPE_MASK(tooltip)
        CHECK_TYPE_MASK(notification)
        CHECK_TYPE_MASK(combo_box)
        CHECK_TYPE_MASK(dnd_icon)
        CHECK_TYPE_MASK(on_screen_display)
        CHECK_TYPE_MASK(critical_notification)
        CHECK_TYPE_MASK(applet_popup)
#undef CHECK_TYPE_MASK
    default:
        break;
    }
    return false;
}

}
