/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/xcb/property.h"

namespace KWin::win::x11
{

template<typename Win>
base::x11::xcb::string_property fetch_application_menu_service_name(Win* win)
{
    return base::x11::xcb::string_property(win->xcb_windows.client,
                                           win->space.atoms->kde_net_wm_appmenu_service_name);
}

template<typename Win>
void read_application_menu_service_name(Win* win, base::x11::xcb::string_property& property)
{
    auto const appmenu = win->control->application_menu();
    win->control->update_application_menu(
        {QString::fromUtf8(property).toStdString(), appmenu.address.path});
}

template<typename Win>
void check_application_menu_service_name(Win* win)
{
    auto property = fetch_application_menu_service_name(win);
    read_application_menu_service_name(win, property);
}

template<typename Win>
base::x11::xcb::string_property fetch_application_menu_object_path(Win* win)
{
    return base::x11::xcb::string_property(win->xcb_windows.client,
                                           win->space.atoms->kde_net_wm_appmenu_object_path);
}

template<typename Win>
void read_application_menu_object_path(Win* win, base::x11::xcb::string_property& property)
{
    auto const appmenu = win->control->application_menu();
    win->control->update_application_menu(
        {appmenu.address.name, QString::fromUtf8(property).toStdString()});
}

template<typename Win>
void check_application_menu_object_path(Win* win)
{
    auto property = fetch_application_menu_object_path(win);
    read_application_menu_object_path(win, property);
}

}
