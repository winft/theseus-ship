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
#include "appmenu.h"

#include "appmenu_interface.h"

#include "win/appmenu.h"
#include "win/deco.h"
#include "win/deco/bridge.h"
#include "win/space.h"

#include <QDBusObjectPath>
#include <QDBusServiceWatcher>

#include <KDecoration2/DecorationSettings>

namespace KWin::win::dbus
{

static const QString s_viewService(QStringLiteral("org.kde.kappmenuview"));

appmenu::appmenu(win::space& space)
    : dbus_iface{std::make_unique<OrgKdeKappmenuInterface>(QStringLiteral("org.kde.kappmenu"),
                                                           QStringLiteral("/KAppMenu"),
                                                           QDBusConnection::sessionBus())}
    , dbus_watcher{std::make_unique<QDBusServiceWatcher>(
          QStringLiteral("org.kde.kappmenu"),
          QDBusConnection::sessionBus(),
          QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration)}
    , space{space}
{
    QObject::connect(
        dbus_iface.get(), &OrgKdeKappmenuInterface::showRequest, this, &appmenu::slotShowRequest);
    QObject::connect(
        dbus_iface.get(), &OrgKdeKappmenuInterface::menuShown, this, &appmenu::slotMenuShown);
    QObject::connect(
        dbus_iface.get(), &OrgKdeKappmenuInterface::menuHidden, this, &appmenu::slotMenuHidden);

    QObject::connect(dbus_watcher.get(), &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        m_applicationMenuEnabled = true;
        Q_EMIT applicationMenuEnabledChanged(true);
    });
    QObject::connect(dbus_watcher.get(), &QDBusServiceWatcher::serviceUnregistered, this, [this]() {
        m_applicationMenuEnabled = false;
        Q_EMIT applicationMenuEnabledChanged(false);
    });

    m_applicationMenuEnabled = QDBusConnection::sessionBus().interface()->isServiceRegistered(
        QStringLiteral("org.kde.kappmenu"));
}

appmenu::~appmenu() = default;

bool appmenu::applicationMenuEnabled() const
{
    return m_applicationMenuEnabled;
}

void appmenu::setViewEnabled(bool enabled)
{
    if (enabled) {
        QDBusConnection::sessionBus().interface()->registerService(
            s_viewService,
            QDBusConnectionInterface::QueueService,
            QDBusConnectionInterface::DontAllowReplacement);
    } else {
        QDBusConnection::sessionBus().interface()->unregisterService(s_viewService);
    }
}

void appmenu::slotShowRequest(const QString& serviceName,
                              const QDBusObjectPath& menuObjectPath,
                              int actionId)
{
    // Ignore show request when user has not configured the application menu title bar button
    auto deco_settings = space.deco->settings();
    auto menu_enum = KDecoration2::DecorationButtonType::ApplicationMenu;
    auto not_left = deco_settings && !deco_settings->decorationButtonsLeft().contains(menu_enum);
    auto not_right = deco_settings && !deco_settings->decorationButtonsRight().contains(menu_enum);
    if (not_left && not_right) {
        return;
    }

    if (auto c = findAbstractClientWithApplicationMenu(serviceName, menuObjectPath)) {
        win::show_application_menu(c, actionId);
    }
}

void appmenu::slotMenuShown(const QString& serviceName, const QDBusObjectPath& menuObjectPath)
{
    if (auto c = findAbstractClientWithApplicationMenu(serviceName, menuObjectPath)) {
        c->control->set_application_menu_active(true);
    }
}

void appmenu::slotMenuHidden(const QString& serviceName, const QDBusObjectPath& menuObjectPath)
{
    if (auto c = findAbstractClientWithApplicationMenu(serviceName, menuObjectPath)) {
        c->control->set_application_menu_active(false);
    }
}

void appmenu::showApplicationMenu(const QPoint& p, Toplevel* window, int actionId)
{
    if (!window->control->has_application_menu()) {
        return;
    }

    auto const menu = window->control->application_menu();
    dbus_iface->showMenu(p.x(),
                         p.y(),
                         QString::fromStdString(menu.address.name),
                         QDBusObjectPath(QString::fromStdString(menu.address.path)),
                         actionId);
}

Toplevel* appmenu::findAbstractClientWithApplicationMenu(const QString& serviceName,
                                                         const QDBusObjectPath& menuObjectPath)
{
    if (serviceName.isEmpty() || menuObjectPath.path().isEmpty()) {
        return nullptr;
    }

    return find_window_with_appmenu(
        space, appmenu_address{serviceName.toStdString(), menuObjectPath.path().toStdString()});
}

}
