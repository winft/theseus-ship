/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_SCREENLOCKERWATCHER_H
#define KWIN_SCREENLOCKERWATCHER_H

#include <QObject>

#include <kwinglobals.h>

class OrgFreedesktopScreenSaverInterface;
class OrgKdeScreensaverInterface;
class QDBusServiceWatcher;
class QDBusPendingCallWatcher;

namespace KWin
{

class KWIN_EXPORT screen_locker_watcher : public QObject
{
    Q_OBJECT
public:
    ~screen_locker_watcher() override;
    void initialize();

    bool is_locked() const
    {
        return m_locked;
    }

Q_SIGNALS:
    void locked(bool locked);
    void about_to_lock();

private Q_SLOTS:
    void set_locked(bool activated);
    void active_queried(QDBusPendingCallWatcher* watcher);
    void service_owner_changed(const QString& service_name,
                               const QString& old_owner,
                               const QString& new_owner);
    void service_registered_queried();
    void service_owner_queried();

private:
    OrgFreedesktopScreenSaverInterface* m_interface = nullptr;
    OrgKdeScreensaverInterface* m_kdeInterface = nullptr;
    QDBusServiceWatcher* m_serviceWatcher;
    bool m_locked;

    KWIN_SINGLETON(screen_locker_watcher)
};

}

#endif
