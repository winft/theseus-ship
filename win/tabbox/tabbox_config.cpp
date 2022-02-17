/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2009 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

#include "tabbox_config.h"

namespace KWin
{
namespace win
{
class tabbox_config_private
{
public:
    tabbox_config_private()
        : show_tabbox(tabbox_config::default_show_tabbox())
        , highlight_windows(tabbox_config::default_highlight_window())
        , tabbox_mode(tabbox_config::ClientTabBox)
        , client_desktop_mode(tabbox_config::default_desktop_mode())
        , client_applications_mode(tabbox_config::default_applications_mode())
        , client_minimized_mode(tabbox_config::default_minimized_mode())
        , show_desktop_mode(tabbox_config::default_show_desktop_mode())
        , client_multi_screen_mode(tabbox_config::default_multi_screen_mode())
        , client_switching_mode(tabbox_config::default_switching_mode())
        , desktop_switching_mode(tabbox_config::MostRecentlyUsedDesktopSwitching)
        , layout_name(tabbox_config::default_layout_name())
    {
    }
    ~tabbox_config_private()
    {
    }
    bool show_tabbox;
    bool highlight_windows;

    tabbox_config::TabBoxMode tabbox_mode;
    tabbox_config::ClientDesktopMode client_desktop_mode;
    tabbox_config::ClientApplicationsMode client_applications_mode;
    tabbox_config::ClientMinimizedMode client_minimized_mode;
    tabbox_config::ShowDesktopMode show_desktop_mode;
    tabbox_config::ClientMultiScreenMode client_multi_screen_mode;
    tabbox_config::ClientSwitchingMode client_switching_mode;
    tabbox_config::DesktopSwitchingMode desktop_switching_mode;
    QString layout_name;
};

tabbox_config::tabbox_config()
    : d(new tabbox_config_private)
{
}

tabbox_config::~tabbox_config()
{
    delete d;
}

tabbox_config& tabbox_config::operator=(const KWin::win::tabbox_config& object)
{
    d->show_tabbox = object.is_show_tabbox();
    d->highlight_windows = object.is_highlight_windows();
    d->tabbox_mode = object.tabbox_mode();
    d->client_desktop_mode = object.client_desktop_mode();
    d->client_applications_mode = object.client_applications_mode();
    d->client_minimized_mode = object.client_minimized_mode();
    d->show_desktop_mode = object.show_desktop_mode();
    d->client_multi_screen_mode = object.client_multi_screen_mode();
    d->client_switching_mode = object.client_switching_mode();
    d->desktop_switching_mode = object.desktop_switching_mode();
    d->layout_name = object.layout_name();
    return *this;
}

void tabbox_config::set_highlight_windows(bool highlight)
{
    d->highlight_windows = highlight;
}

bool tabbox_config::is_highlight_windows() const
{
    return d->highlight_windows;
}

void tabbox_config::set_show_tabbox(bool show)
{
    d->show_tabbox = show;
}

bool tabbox_config::is_show_tabbox() const
{
    return d->show_tabbox;
}

void tabbox_config::set_tabbox_mode(tabbox_config::TabBoxMode mode)
{
    d->tabbox_mode = mode;
}

tabbox_config::TabBoxMode tabbox_config::tabbox_mode() const
{
    return d->tabbox_mode;
}

tabbox_config::ClientDesktopMode tabbox_config::client_desktop_mode() const
{
    return d->client_desktop_mode;
}

void tabbox_config::set_client_desktop_mode(ClientDesktopMode desktop_mode)
{
    d->client_desktop_mode = desktop_mode;
}

tabbox_config::ClientApplicationsMode tabbox_config::client_applications_mode() const
{
    return d->client_applications_mode;
}

void tabbox_config::set_client_applications_mode(ClientApplicationsMode applications_mode)
{
    d->client_applications_mode = applications_mode;
}

tabbox_config::ClientMinimizedMode tabbox_config::client_minimized_mode() const
{
    return d->client_minimized_mode;
}

void tabbox_config::set_client_minimized_mode(ClientMinimizedMode minimized_mode)
{
    d->client_minimized_mode = minimized_mode;
}

tabbox_config::ShowDesktopMode tabbox_config::show_desktop_mode() const
{
    return d->show_desktop_mode;
}

void tabbox_config::set_show_desktop_mode(ShowDesktopMode show_desktop_mode)
{
    d->show_desktop_mode = show_desktop_mode;
}

tabbox_config::ClientMultiScreenMode tabbox_config::client_multi_screen_mode() const
{
    return d->client_multi_screen_mode;
}

void tabbox_config::set_client_multi_screen_mode(ClientMultiScreenMode multi_screen_mode)
{
    d->client_multi_screen_mode = multi_screen_mode;
}

tabbox_config::ClientSwitchingMode tabbox_config::client_switching_mode() const
{
    return d->client_switching_mode;
}

void tabbox_config::set_client_switching_mode(ClientSwitchingMode switching_mode)
{
    d->client_switching_mode = switching_mode;
}

tabbox_config::DesktopSwitchingMode tabbox_config::desktop_switching_mode() const
{
    return d->desktop_switching_mode;
}

void tabbox_config::set_desktop_switching_mode(DesktopSwitchingMode switching_mode)
{
    d->desktop_switching_mode = switching_mode;
}

QString& tabbox_config::layout_name() const
{
    return d->layout_name;
}

void tabbox_config::set_layout_name(const QString& name)
{
    d->layout_name = name;
}

} // namespace win
} // namespace KWin
