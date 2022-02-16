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
class TabBoxConfigPrivate
{
public:
    TabBoxConfigPrivate()
        : show_tabbox(TabBoxConfig::default_show_tabbox())
        , highlight_windows(TabBoxConfig::default_highlight_window())
        , tabbox_mode(TabBoxConfig::ClientTabBox)
        , client_desktop_mode(TabBoxConfig::default_desktop_mode())
        , client_applications_mode(TabBoxConfig::default_applications_mode())
        , client_minimized_mode(TabBoxConfig::default_minimized_mode())
        , show_desktop_mode(TabBoxConfig::default_show_desktop_mode())
        , client_multi_screen_mode(TabBoxConfig::default_multi_screen_mode())
        , client_switching_mode(TabBoxConfig::default_switching_mode())
        , desktop_switching_mode(TabBoxConfig::MostRecentlyUsedDesktopSwitching)
        , layout_name(TabBoxConfig::default_layout_name())
    {
    }
    ~TabBoxConfigPrivate()
    {
    }
    bool show_tabbox;
    bool highlight_windows;

    TabBoxConfig::TabBoxMode tabbox_mode;
    TabBoxConfig::ClientDesktopMode client_desktop_mode;
    TabBoxConfig::ClientApplicationsMode client_applications_mode;
    TabBoxConfig::ClientMinimizedMode client_minimized_mode;
    TabBoxConfig::ShowDesktopMode show_desktop_mode;
    TabBoxConfig::ClientMultiScreenMode client_multi_screen_mode;
    TabBoxConfig::ClientSwitchingMode client_switching_mode;
    TabBoxConfig::DesktopSwitchingMode desktop_switching_mode;
    QString layout_name;
};

TabBoxConfig::TabBoxConfig()
    : d(new TabBoxConfigPrivate)
{
}

TabBoxConfig::~TabBoxConfig()
{
    delete d;
}

TabBoxConfig& TabBoxConfig::operator=(const KWin::win::TabBoxConfig& object)
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

void TabBoxConfig::set_highlight_windows(bool highlight)
{
    d->highlight_windows = highlight;
}

bool TabBoxConfig::is_highlight_windows() const
{
    return d->highlight_windows;
}

void TabBoxConfig::set_show_tabbox(bool show)
{
    d->show_tabbox = show;
}

bool TabBoxConfig::is_show_tabbox() const
{
    return d->show_tabbox;
}

void TabBoxConfig::set_tabbox_mode(TabBoxConfig::TabBoxMode mode)
{
    d->tabbox_mode = mode;
}

TabBoxConfig::TabBoxMode TabBoxConfig::tabbox_mode() const
{
    return d->tabbox_mode;
}

TabBoxConfig::ClientDesktopMode TabBoxConfig::client_desktop_mode() const
{
    return d->client_desktop_mode;
}

void TabBoxConfig::set_client_desktop_mode(ClientDesktopMode desktop_mode)
{
    d->client_desktop_mode = desktop_mode;
}

TabBoxConfig::ClientApplicationsMode TabBoxConfig::client_applications_mode() const
{
    return d->client_applications_mode;
}

void TabBoxConfig::set_client_applications_mode(ClientApplicationsMode applications_mode)
{
    d->client_applications_mode = applications_mode;
}

TabBoxConfig::ClientMinimizedMode TabBoxConfig::client_minimized_mode() const
{
    return d->client_minimized_mode;
}

void TabBoxConfig::set_client_minimized_mode(ClientMinimizedMode minimized_mode)
{
    d->client_minimized_mode = minimized_mode;
}

TabBoxConfig::ShowDesktopMode TabBoxConfig::show_desktop_mode() const
{
    return d->show_desktop_mode;
}

void TabBoxConfig::set_show_desktop_mode(ShowDesktopMode show_desktop_mode)
{
    d->show_desktop_mode = show_desktop_mode;
}

TabBoxConfig::ClientMultiScreenMode TabBoxConfig::client_multi_screen_mode() const
{
    return d->client_multi_screen_mode;
}

void TabBoxConfig::set_client_multi_screen_mode(ClientMultiScreenMode multi_screen_mode)
{
    d->client_multi_screen_mode = multi_screen_mode;
}

TabBoxConfig::ClientSwitchingMode TabBoxConfig::client_switching_mode() const
{
    return d->client_switching_mode;
}

void TabBoxConfig::set_client_switching_mode(ClientSwitchingMode switching_mode)
{
    d->client_switching_mode = switching_mode;
}

TabBoxConfig::DesktopSwitchingMode TabBoxConfig::desktop_switching_mode() const
{
    return d->desktop_switching_mode;
}

void TabBoxConfig::set_desktop_switching_mode(DesktopSwitchingMode switching_mode)
{
    d->desktop_switching_mode = switching_mode;
}

QString& TabBoxConfig::layout_name() const
{
    return d->layout_name;
}

void TabBoxConfig::set_layout_name(const QString& name)
{
    d->layout_name = name;
}

} // namespace win
} // namespace KWin
