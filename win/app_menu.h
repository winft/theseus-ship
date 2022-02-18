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

#include "kwinglobals.h"

#include <QObject>
#include <xcb/xcb.h>

class QPoint;
class OrgKdeKappmenuInterface;
class QDBusObjectPath;
class QDBusServiceWatcher;

namespace KWin
{
class Toplevel;

namespace win
{
class KWIN_EXPORT app_menu : public QObject
{
    Q_OBJECT

public:
    ~app_menu() override;

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
    OrgKdeKappmenuInterface* m_appmenuInterface;
    QDBusServiceWatcher* m_kappMenuWatcher;

    Toplevel* findAbstractClientWithApplicationMenu(const QString& serviceName,
                                                    const QDBusObjectPath& menuObjectPath);

    bool m_applicationMenuEnabled = false;

    KWIN_SINGLETON(app_menu)
};

}
}
