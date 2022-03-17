/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "screen_locker_watcher.h"

#include "main.h"

// dbus generated
#include "kscreenlocker_interface.h"
#include "screenlocker_interface.h"

namespace KWin::desktop
{

static QString const SCREEN_LOCKER_SERVICE_NAME = QStringLiteral("org.freedesktop.ScreenSaver");

screen_locker_watcher::screen_locker_watcher()
    : m_serviceWatcher{new QDBusServiceWatcher(this)}
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

    m_interface = new OrgFreedesktopScreenSaverInterface(SCREEN_LOCKER_SERVICE_NAME,
                                                         QStringLiteral("/ScreenSaver"),
                                                         QDBusConnection::sessionBus(),
                                                         this);
    m_kdeInterface = new OrgKdeScreensaverInterface(SCREEN_LOCKER_SERVICE_NAME,
                                                    QStringLiteral("/ScreenSaver"),
                                                    QDBusConnection::sessionBus(),
                                                    this);
    connect(m_interface,
            &OrgFreedesktopScreenSaverInterface::ActiveChanged,
            this,
            &screen_locker_watcher::set_locked);
    connect(m_kdeInterface,
            &OrgKdeScreensaverInterface::AboutToLock,
            this,
            &screen_locker_watcher::about_to_lock);

    query_active();
}

void screen_locker_watcher::service_owner_changed(QString const& /*service_name*/,
                                                  QString const& /*old_owner*/,
                                                  QString const& new_owner)
{
    m_locked = false;
    if (!new_owner.isEmpty()) {
        query_active();
    }
}

void screen_locker_watcher::query_active()
{
    QDBusPendingCallWatcher* watcher = new QDBusPendingCallWatcher(m_interface->GetActive(), this);
    connect(
        watcher, &QDBusPendingCallWatcher::finished, this, &screen_locker_watcher::active_queried);
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
