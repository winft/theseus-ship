/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QObject>

class OrgFreedesktopScreenSaverInterface;
class OrgKdeScreensaverInterface;
class QDBusServiceWatcher;
class QDBusPendingCallWatcher;

namespace KWin::desktop
{

class KWIN_EXPORT screen_locker_watcher : public QObject
{
    Q_OBJECT
public:
    screen_locker_watcher();

    void initialize();
    bool is_locked() const;

Q_SIGNALS:
    void locked(bool locked);
    void about_to_lock();

private:
    void set_locked(bool lock);
    void active_queried(QDBusPendingCallWatcher* watcher);
    void service_owner_changed(QString const& service_name,
                               QString const& old_owner,
                               QString const& new_owner);
    void query_active();

    OrgFreedesktopScreenSaverInterface* m_interface{nullptr};
    OrgKdeScreensaverInterface* m_kdeInterface{nullptr};
    QDBusServiceWatcher* m_serviceWatcher;
    bool m_locked{false};
};

}
