/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "screenlockerwatcher.h"

#include "main.h"

#include <QFutureWatcher>
#include <QtConcurrentRun>

// dbus generated
#include "kscreenlocker_interface.h"
#include "screenlocker_interface.h"

namespace KWin
{

KWIN_SINGLETON_FACTORY(screen_locker_watcher)

static const QString SCREEN_LOCKER_SERVICE_NAME = QStringLiteral("org.freedesktop.ScreenSaver");

screen_locker_watcher::screen_locker_watcher(QObject* parent)
    : QObject(parent)
    , m_serviceWatcher(new QDBusServiceWatcher(this))
    , m_locked(false)
{
}

screen_locker_watcher::~screen_locker_watcher()
{
}

void screen_locker_watcher::initialize()
{
    connect(m_serviceWatcher,
            &QDBusServiceWatcher::serviceOwnerChanged,
            this,
            &screen_locker_watcher::service_owner_changed);
    m_serviceWatcher->setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);
    m_serviceWatcher->addWatchedService(SCREEN_LOCKER_SERVICE_NAME);
    // check whether service is registered
    QFutureWatcher<QDBusReply<bool>>* watcher = new QFutureWatcher<QDBusReply<bool>>(this);
    connect(watcher,
            &QFutureWatcher<QDBusReply<bool>>::finished,
            this,
            &screen_locker_watcher::service_registered_queried);
    connect(watcher,
            &QFutureWatcher<QDBusReply<bool>>::canceled,
            watcher,
            &QFutureWatcher<QDBusReply<bool>>::deleteLater);
    watcher->setFuture(QtConcurrent::run(QDBusConnection::sessionBus().interface(),
                                         &QDBusConnectionInterface::isServiceRegistered,
                                         SCREEN_LOCKER_SERVICE_NAME));
}

void screen_locker_watcher::service_owner_changed(const QString& service_name,
                                                  const QString& /*old_owner*/,
                                                  const QString& new_owner)
{
    if (service_name != SCREEN_LOCKER_SERVICE_NAME) {
        return;
    }
    delete m_interface;
    m_interface = nullptr;
    delete m_kdeInterface;
    m_kdeInterface = nullptr;

    m_locked = false;
    if (!new_owner.isEmpty()) {
        m_interface = new OrgFreedesktopScreenSaverInterface(
            new_owner, QStringLiteral("/ScreenSaver"), QDBusConnection::sessionBus(), this);
        m_kdeInterface = new OrgKdeScreensaverInterface(
            new_owner, QStringLiteral("/ScreenSaver"), QDBusConnection::sessionBus(), this);
        connect(m_interface,
                &OrgFreedesktopScreenSaverInterface::ActiveChanged,
                this,
                &screen_locker_watcher::set_locked);
        QDBusPendingCallWatcher* watcher
            = new QDBusPendingCallWatcher(m_interface->GetActive(), this);
        connect(watcher,
                &QDBusPendingCallWatcher::finished,
                this,
                &screen_locker_watcher::active_queried);
        connect(m_kdeInterface,
                &OrgKdeScreensaverInterface::AboutToLock,
                this,
                &screen_locker_watcher::about_to_lock);
    }
}

void screen_locker_watcher::service_registered_queried()
{
    QFutureWatcher<QDBusReply<bool>>* watcher
        = dynamic_cast<QFutureWatcher<QDBusReply<bool>>*>(sender());
    if (!watcher) {
        return;
    }
    const QDBusReply<bool>& reply = watcher->result();
    if (reply.isValid() && reply.value()) {
        QFutureWatcher<QDBusReply<QString>>* ownerWatcher
            = new QFutureWatcher<QDBusReply<QString>>(this);
        connect(ownerWatcher,
                &QFutureWatcher<QDBusReply<QString>>::finished,
                this,
                &screen_locker_watcher::service_owner_queried);
        connect(ownerWatcher,
                &QFutureWatcher<QDBusReply<QString>>::canceled,
                ownerWatcher,
                &QFutureWatcher<QDBusReply<QString>>::deleteLater);
        ownerWatcher->setFuture(QtConcurrent::run(QDBusConnection::sessionBus().interface(),
                                                  &QDBusConnectionInterface::serviceOwner,
                                                  SCREEN_LOCKER_SERVICE_NAME));
    }
    watcher->deleteLater();
}

void screen_locker_watcher::service_owner_queried()
{
    QFutureWatcher<QDBusReply<QString>>* watcher
        = dynamic_cast<QFutureWatcher<QDBusReply<QString>>*>(sender());
    if (!watcher) {
        return;
    }
    const QDBusReply<QString> reply = watcher->result();
    if (reply.isValid()) {
        service_owner_changed(SCREEN_LOCKER_SERVICE_NAME, QString(), reply.value());
    }

    watcher->deleteLater();
}

void screen_locker_watcher::active_queried(QDBusPendingCallWatcher* watcher)
{
    QDBusPendingReply<bool> reply = *watcher;
    if (!reply.isError()) {
        set_locked(reply.value());
    }
    watcher->deleteLater();
}

void screen_locker_watcher::set_locked(bool activated)
{
    if (m_locked == activated) {
        return;
    }
    m_locked = activated;
    Q_EMIT locked(m_locked);
}

}
