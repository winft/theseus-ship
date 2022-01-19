/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2012 Martin Gräßlin <mgraesslin@kde.org>

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

#ifndef KWIN_DBUS_INTERFACE_H
#define KWIN_DBUS_INTERFACE_H

#include <QObject>
#include <QtDBus>

#include "virtualdesktopsdbustypes.h"

namespace KWin
{

namespace win
{
class virtual_desktop_manager;
}

/**
 * @brief This class is a wrapper for the org.kde.KWin D-Bus interface.
 *
 * The main purpose of this class is to be exported on the D-Bus as object /KWin.
 * It is a pure wrapper to provide the deprecated D-Bus methods which have been
 * removed from Workspace which used to implement the complete D-Bus interface.
 *
 * Nowadays the D-Bus interfaces are distributed, parts of it are exported on
 * /Compositor, parts on /Effects and parts on /KWin. The implementation in this
 * class just delegates the method calls to the actual implementation in one of the
 * three singletons.
 *
 * @author Martin Gräßlin <mgraesslin@kde.org>
 */
class DBusInterface: public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin")
public:
    explicit DBusInterface(QObject *parent);
    ~DBusInterface() override;

public: // PROPERTIES
public Q_SLOTS: // METHODS
    int currentDesktop();
    Q_NOREPLY void killWindow();
    void nextDesktop();
    void previousDesktop();
    Q_NOREPLY void reconfigure();
    bool setCurrentDesktop(int desktop);
    bool startActivity(const QString &in0);
    bool stopActivity(const QString &in0);
    QString supportInformation();
    Q_NOREPLY void unclutterDesktop();
    Q_NOREPLY void showDebugConsole();
    void enableFtrace(bool enable);

    QVariantMap queryWindowInfo();
    QVariantMap getWindowInfo(const QString &uuid);

private Q_SLOTS:
    void becomeKWinService(const QString &service);

private:
    void announceService();
    QString m_serviceName;
    QDBusMessage m_replyQueryWindowInfo;
};

//TODO: disable all of this in case of kiosk?

class VirtualDesktopManagerDBusInterface : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.VirtualDesktopManager")

    /**
     * The number of virtual desktops currently available.
     * The ids of the virtual desktops are in the range [1, win::virtual_desktop_manager::maximum()].
     */
    Q_PROPERTY(uint count READ count NOTIFY countChanged)
    /**
     * The number of rows the virtual desktops will be laid out in
     */
    Q_PROPERTY(uint rows READ rows WRITE setRows NOTIFY rowsChanged)
    /**
     * The id of the virtual desktop which is currently in use.
     */
    Q_PROPERTY(QString current READ current WRITE setCurrent NOTIFY currentChanged)
    /**
     * Whether navigation in the desktop layout wraps around at the borders.
     */
    Q_PROPERTY(bool navigationWrappingAround READ isNavigationWrappingAround WRITE setNavigationWrappingAround NOTIFY navigationWrappingAroundChanged)

    /**
     * list of key/value pairs which every one of them is representing a desktop
     */
    Q_PROPERTY(KWin::win::dbus::virtual_desktop_data_vector desktops READ desktops NOTIFY desktopsChanged);

public:
    VirtualDesktopManagerDBusInterface(win::virtual_desktop_manager* parent);
    ~VirtualDesktopManagerDBusInterface() override = default;

    uint count() const;

    void setRows(uint rows);
    uint rows() const;

    void setCurrent(const QString &id);
    QString current() const;

    void setNavigationWrappingAround(bool wraps);
    bool isNavigationWrappingAround() const;

    KWin::win::dbus::virtual_desktop_data_vector desktops() const;

Q_SIGNALS:
    void countChanged(uint count);
    void rowsChanged(uint rows);
    void currentChanged(const QString &id);
    void navigationWrappingAroundChanged(bool wraps);
    void desktopsChanged(KWin::win::dbus::virtual_desktop_data_vector);
    void desktopDataChanged(const QString &id, KWin::win::dbus::virtual_desktop_data);
    void desktopCreated(const QString &id, KWin::win::dbus::virtual_desktop_data);
    void desktopRemoved(const QString &id);

public Q_SLOTS:
    /**
     * Create a desktop with a new name at a given position
     * note: the position starts from 1
     */
    void createDesktop(uint position, const QString &name);
    void setDesktopName(const QString &id, const QString &name);
    void removeDesktop(const QString &id);

private:
    win::virtual_desktop_manager* m_manager;
};

} // namespace

#endif // KWIN_DBUS_INTERFACE_H
