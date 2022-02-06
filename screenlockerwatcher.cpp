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

static QString const SCREEN_LOCKER_SERVICE_NAME = QStringLiteral("org.freedesktop.ScreenSaver");

screen_locker_watcher::screen_locker_watcher(QObject* parent)
    : QObject(parent)
    , m_serviceWatcher{new QDBusServiceWatcher(this)}
{
}

void screen_locker_watcher::initialize()
{
    QObject::connect(m_serviceWatcher,
                     &QDBusServiceWatcher::serviceOwnerChanged,
                     this,
                     &screen_locker_watcher::service_owner_changed);

    m_serviceWatcher->setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);
    m_serviceWatcher->addWatchedService(SCREEN_LOCKER_SERVICE_NAME);

    // check whether service is registered
    using future_reply = QFutureWatcher<QDBusReply<bool>>;
    auto watcher = new future_reply(this);

    QObject::connect(
        watcher, &future_reply::finished, this, &screen_locker_watcher::service_registered_queried);
    QObject::connect(watcher, &future_reply::canceled, watcher, &future_reply::deleteLater);

    watcher->setFuture(QtConcurrent::run(QDBusConnection::sessionBus().interface(),
                                         &QDBusConnectionInterface::isServiceRegistered,
                                         SCREEN_LOCKER_SERVICE_NAME));
}

void screen_locker_watcher::service_owner_changed(QString const& service_name,
                                                  QString const& /*old_owner*/,
                                                  QString const& new_owner)
{
    if (service_name != SCREEN_LOCKER_SERVICE_NAME) {
        return;
    }

    delete m_interface;
    m_interface = nullptr;
    delete m_kdeInterface;
    m_kdeInterface = nullptr;

    m_locked = false;

    if (new_owner.isEmpty()) {
        return;
    }

    m_interface = new OrgFreedesktopScreenSaverInterface(
        new_owner, QStringLiteral("/ScreenSaver"), QDBusConnection::sessionBus(), this);
    m_kdeInterface = new OrgKdeScreensaverInterface(
        new_owner, QStringLiteral("/ScreenSaver"), QDBusConnection::sessionBus(), this);

    QObject::connect(m_interface,
                     &OrgFreedesktopScreenSaverInterface::ActiveChanged,
                     this,
                     &screen_locker_watcher::set_locked);

    auto watcher = new QDBusPendingCallWatcher(m_interface->GetActive(), this);
    QObject::connect(
        watcher, &QDBusPendingCallWatcher::finished, this, &screen_locker_watcher::active_queried);
    QObject::connect(m_kdeInterface,
                     &OrgKdeScreensaverInterface::AboutToLock,
                     this,
                     &screen_locker_watcher::about_to_lock);
}

void screen_locker_watcher::service_registered_queried()
{
    auto watcher = dynamic_cast<QFutureWatcher<QDBusReply<bool>>*>(sender());
    if (!watcher) {
        return;
    }

    using future_reply = QFutureWatcher<QDBusReply<QString>>;
    auto const& reply = watcher->result();

    if (reply.isValid() && reply.value()) {
        auto owner_watcher = new future_reply(this);

        QObject::connect(owner_watcher,
                         &future_reply::finished,
                         this,
                         &screen_locker_watcher::service_owner_queried);
        QObject::connect(
            owner_watcher, &future_reply::canceled, owner_watcher, &future_reply::deleteLater);

        owner_watcher->setFuture(QtConcurrent::run(QDBusConnection::sessionBus().interface(),
                                                   &QDBusConnectionInterface::serviceOwner,
                                                   SCREEN_LOCKER_SERVICE_NAME));
    }

    watcher->deleteLater();
}

void screen_locker_watcher::service_owner_queried()
{
    auto watcher = dynamic_cast<QFutureWatcher<QDBusReply<QString>>*>(sender());
    if (!watcher) {
        return;
    }

    auto const reply = watcher->result();
    if (reply.isValid()) {
        service_owner_changed(SCREEN_LOCKER_SERVICE_NAME, QString(), reply.value());
    }

    watcher->deleteLater();
}

void screen_locker_watcher::active_queried(QDBusPendingCallWatcher* watcher)
{
    auto reply = static_cast<QDBusPendingReply<bool>>(*watcher);
    if (!reply.isError()) {
        set_locked(reply.value());
    }

    watcher->deleteLater();
}

bool screen_locker_watcher::is_locked() const
{
    return m_locked;
}

void screen_locker_watcher::set_locked(bool lock)
{
    if (m_locked == lock) {
        return;
    }

    m_locked = lock;
    Q_EMIT locked(lock);
}

}
