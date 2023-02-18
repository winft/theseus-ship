/*
SPDX-FileCopyrightText: 2011 Lionel Chauvin <megabigbug@yahoo.fr>
SPDX-FileCopyrightText: 2011, 2012 Cédric Bellegarde <gnumdk@gmail.com>
SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "appmenu.h"

#include "appmenu_interface.h"

#include "win/deco/bridge.h"

#include <QDBusObjectPath>
#include <QDBusServiceWatcher>

namespace KWin::win::dbus
{

static const QString s_viewService(QStringLiteral("org.kde.kappmenuview"));

appmenu::appmenu(appmenu_callbacks callbacks)
    : dbus_iface{std::make_unique<OrgKdeKappmenuInterface>(QStringLiteral("org.kde.kappmenu"),
                                                           QStringLiteral("/KAppMenu"),
                                                           QDBusConnection::sessionBus())}
    , dbus_watcher{std::make_unique<QDBusServiceWatcher>(
          QStringLiteral("org.kde.kappmenu"),
          QDBusConnection::sessionBus(),
          QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration)}
    , callbacks{callbacks}
{
    assert(callbacks.show_request);
    assert(callbacks.visibility);

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

bool is_address_valid(appmenu_address const& addr)
{
    return !addr.name.empty() && !addr.path.empty();
}

appmenu_address get_address(QString const& name, QDBusObjectPath const& objpath)
{
    return {name.toStdString(), objpath.path().toStdString()};
}

void appmenu::slotShowRequest(const QString& serviceName,
                              const QDBusObjectPath& menuObjectPath,
                              int actionId)
{
    if (auto const addr = get_address(serviceName, menuObjectPath); is_address_valid(addr)) {
        callbacks.show_request(addr, actionId);
    }
}

void appmenu::slotMenuShown(const QString& serviceName, const QDBusObjectPath& menuObjectPath)
{
    if (auto const addr = get_address(serviceName, menuObjectPath); is_address_valid(addr)) {
        callbacks.visibility(addr, true);
    }
}

void appmenu::slotMenuHidden(const QString& serviceName, const QDBusObjectPath& menuObjectPath)
{
    if (auto const addr = get_address(serviceName, menuObjectPath); is_address_valid(addr)) {
        callbacks.visibility(addr, false);
    }
}

void appmenu::showApplicationMenu(const QPoint& p, win::appmenu const& data, int actionId)
{
    dbus_iface->showMenu(p.x(),
                         p.y(),
                         QString::fromStdString(data.address.name),
                         QDBusObjectPath(QString::fromStdString(data.address.path)),
                         actionId);
}

}
