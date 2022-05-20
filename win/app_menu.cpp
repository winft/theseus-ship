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
#include "app_menu.h"

#include "appmenu_interface.h"
#include "deco.h"
#include "deco/bridge.h"
#include "space.h"

#include <QDBusObjectPath>
#include <QDBusServiceWatcher>

#include <KDecoration2/DecorationSettings>

using namespace KWin;
using namespace KWin::win;

KWIN_SINGLETON_FACTORY(app_menu)

static const QString s_viewService(QStringLiteral("org.kde.kappmenuview"));

app_menu::app_menu(QObject* parent)
    : QObject(parent)
    , m_appmenuInterface(new OrgKdeKappmenuInterface(QStringLiteral("org.kde.kappmenu"),
                                                     QStringLiteral("/KAppMenu"),
                                                     QDBusConnection::sessionBus(),
                                                     this))
{
    connect(m_appmenuInterface,
            &OrgKdeKappmenuInterface::showRequest,
            this,
            &app_menu::slotShowRequest);
    connect(
        m_appmenuInterface, &OrgKdeKappmenuInterface::menuShown, this, &app_menu::slotMenuShown);
    connect(
        m_appmenuInterface, &OrgKdeKappmenuInterface::menuHidden, this, &app_menu::slotMenuHidden);

    m_kappMenuWatcher = new QDBusServiceWatcher(QStringLiteral("org.kde.kappmenu"),
                                                QDBusConnection::sessionBus(),
                                                QDBusServiceWatcher::WatchForRegistration
                                                    | QDBusServiceWatcher::WatchForUnregistration,
                                                this);

    connect(m_kappMenuWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        m_applicationMenuEnabled = true;
        Q_EMIT applicationMenuEnabledChanged(true);
    });
    connect(m_kappMenuWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this]() {
        m_applicationMenuEnabled = false;
        Q_EMIT applicationMenuEnabledChanged(false);
    });

    m_applicationMenuEnabled = QDBusConnection::sessionBus().interface()->isServiceRegistered(
        QStringLiteral("org.kde.kappmenu"));
}

app_menu::~app_menu()
{
    s_self = nullptr;
}

bool app_menu::applicationMenuEnabled() const
{
    return m_applicationMenuEnabled;
}

void app_menu::setViewEnabled(bool enabled)
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

void app_menu::slotShowRequest(const QString& serviceName,
                               const QDBusObjectPath& menuObjectPath,
                               int actionId)
{
    // Ignore show request when user has not configured the application menu title bar button
    auto deco_settings = deco::bridge::self()->settings();
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

void app_menu::slotMenuShown(const QString& serviceName, const QDBusObjectPath& menuObjectPath)
{
    if (auto c = findAbstractClientWithApplicationMenu(serviceName, menuObjectPath)) {
        c->control->set_application_menu_active(true);
    }
}

void app_menu::slotMenuHidden(const QString& serviceName, const QDBusObjectPath& menuObjectPath)
{
    if (auto c = findAbstractClientWithApplicationMenu(serviceName, menuObjectPath)) {
        c->control->set_application_menu_active(false);
    }
}

void app_menu::showApplicationMenu(const QPoint& p, Toplevel* window, int actionId)
{
    if (!window->control->has_application_menu()) {
        return;
    }

    auto const& [name, path] = window->control->application_menu();
    m_appmenuInterface->showMenu(p.x(), p.y(), name, QDBusObjectPath(path), actionId);
}

Toplevel* app_menu::findAbstractClientWithApplicationMenu(const QString& serviceName,
                                                          const QDBusObjectPath& menuObjectPath)
{
    if (serviceName.isEmpty() || menuObjectPath.path().isEmpty()) {
        return nullptr;
    }

    auto const addr = std::make_tuple(serviceName, menuObjectPath.path());

    for (auto win : workspace()->m_windows) {
        if (win->control && win->control->application_menu() == addr) {
            return win;
        }
    }
    return nullptr;
}
