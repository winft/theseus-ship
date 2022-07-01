/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (c) 2011 Lionel Chauvin <megabigbug@yahoo.fr>
Copyright (c) 2011,2012 Cédric Bellegarde <gnumdk@gmail.com>
Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

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
#pragma once

#include "kwin_export.h"

#include "win/appmenu.h"

#include <KDecoration2/DecorationSettings>
#include <QObject>
#include <functional>
#include <memory>

class QPoint;
class OrgKdeKappmenuInterface;
class QDBusObjectPath;
class QDBusServiceWatcher;

namespace KWin
{
class Toplevel;

namespace win::dbus
{

struct appmenu_callbacks {
    std::function<void(appmenu_address const&, int action_id)> show_request;
    std::function<void(appmenu_address const&, bool)> visibility;
};

template<typename Space>
appmenu_callbacks create_appmenu_callbacks(Space const& space)
{
    appmenu_callbacks callbacks;

    callbacks.show_request = [&space](appmenu_address const& addr, int action_id) {
        // Ignore show request when user has not configured the application menu title bar button
        auto deco_settings = space.deco->settings();
        auto menu_enum = KDecoration2::DecorationButtonType::ApplicationMenu;
        auto not_left
            = deco_settings && !deco_settings->decorationButtonsLeft().contains(menu_enum);
        auto not_right
            = deco_settings && !deco_settings->decorationButtonsRight().contains(menu_enum);
        if (not_left && not_right) {
            return;
        }

        if (auto win = find_window_with_appmenu(space, addr)) {
            win::show_appmenu(*win, action_id);
        }
    };
    callbacks.visibility = [&space](appmenu_address const& addr, bool active) {
        if (auto win = find_window_with_appmenu(space, addr)) {
            win->control->set_application_menu_active(active);
        }
    };

    return callbacks;
}

class KWIN_EXPORT appmenu : public QObject
{
    Q_OBJECT

public:
    explicit appmenu(appmenu_callbacks callbacks);
    ~appmenu();

    void showApplicationMenu(const QPoint& pos, Toplevel* window, int actionId);

    bool applicationMenuEnabled() const;

    void setViewEnabled(bool enabled);

Q_SIGNALS:
    void applicationMenuEnabledChanged(bool enabled);

private Q_SLOTS:
    void slotShowRequest(const QString& serviceName,
                         const QDBusObjectPath& menuObjectPath,
                         int actionId);
    void slotMenuShown(const QString& serviceName, const QDBusObjectPath& menuObjectPath);
    void slotMenuHidden(const QString& serviceName, const QDBusObjectPath& menuObjectPath);

private:
    std::unique_ptr<OrgKdeKappmenuInterface> dbus_iface;
    std::unique_ptr<QDBusServiceWatcher> dbus_watcher;

    bool m_applicationMenuEnabled = false;
    appmenu_callbacks callbacks;
};

}
}
