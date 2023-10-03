/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"
#include "virtual_desktop_types.h"

#include <QObject>
#include <QtDBus>

namespace KWin::win
{

class subspace_manager;

namespace dbus
{

// TODO: disable all of this in case of kiosk?

class KWIN_EXPORT subspace_manager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.VirtualDesktopManager")

    /**
     * The number of virtual desktops currently available.
     * The ids of the virtual desktops are in the range [1,
     * win::subspace_manager::maximum()].
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
    Q_PROPERTY(bool navigationWrappingAround READ isNavigationWrappingAround WRITE
                   setNavigationWrappingAround NOTIFY navigationWrappingAroundChanged)

    /**
     * list of key/value pairs which every one of them is representing a desktop
     */
    Q_PROPERTY(KWin::win::dbus::subspace_data_vector desktops READ desktops NOTIFY desktopsChanged)

public:
    subspace_manager(win::subspace_manager* parent);
    ~subspace_manager() override = default;

    uint count() const;

    void setRows(uint rows);
    uint rows() const;

    void setCurrent(const QString& id);
    QString current() const;

    void setNavigationWrappingAround(bool wraps);
    bool isNavigationWrappingAround() const;

    subspace_data_vector desktops() const;

Q_SIGNALS:
    void countChanged(uint count);
    void rowsChanged(uint rows);
    void currentChanged(const QString& id);
    void navigationWrappingAroundChanged(bool wraps);
    void desktopsChanged(KWin::win::dbus::subspace_data_vector);
    void desktopDataChanged(const QString& id, KWin::win::dbus::subspace_data);
    void desktopCreated(const QString& id, KWin::win::dbus::subspace_data);
    void desktopRemoved(const QString& id);

public Q_SLOTS:
    /**
     * Create a desktop with a new name at a given position
     * note: the position starts from 1
     */
    void createDesktop(uint position, const QString& name);
    void setDesktopName(const QString& id, const QString& name);
    void removeDesktop(const QString& id);

private:
    win::subspace_manager* m_manager;
};

}
}
