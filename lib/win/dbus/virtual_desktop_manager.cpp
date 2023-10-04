/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "virtual_desktop_manager.h"

#include "virtualdesktopmanageradaptor.h"

#include <win/subspace_manager.h>

namespace KWin::win::dbus
{

subspace_manager_wrap::subspace_manager_wrap(win::subspace_manager_qobject* parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<KWin::win::dbus::subspace_data>();
    qDBusRegisterMetaType<KWin::win::dbus::subspace_data_vector>();

    new VirtualDesktopManagerAdaptor(this);
    QDBusConnection::sessionBus().registerObject(
        QStringLiteral("/VirtualDesktopManager"),
        QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
        this);

    QObject::connect(parent,
                     &win::subspace_manager_qobject::current_changed,
                     this,
                     [this](auto /*prev*/, auto next) { Q_EMIT currentChanged(next->id()); });

    QObject::connect(parent,
                     &win::subspace_manager_qobject::countChanged,
                     this,
                     [this](uint previousCount, uint newCount) {
                         Q_UNUSED(previousCount);
                         Q_EMIT countChanged(newCount);
                         Q_EMIT desktopsChanged(desktops());
                     });

    QObject::connect(
        parent, &win::subspace_manager_qobject::navigationWrappingAroundChanged, this, [this]() {
            Q_EMIT navigationWrappingAroundChanged(isNavigationWrappingAround());
        });

    QObject::connect(parent,
                     &win::subspace_manager_qobject::rowsChanged,
                     this,
                     &subspace_manager_wrap::rowsChanged);

    QObject::connect(
        parent, &win::subspace_manager_qobject::subspace_created, this, [this](auto subsp) {
            assert(subsp);
            add_subspace(*subsp);
        });
    QObject::connect(
        parent, &win::subspace_manager_qobject::subspace_removed, this, [this](auto vd) {
            Q_EMIT desktopRemoved(vd->id());
            Q_EMIT desktopsChanged(desktops());
        });
}

void subspace_manager_wrap::add_subspace(win::subspace& subspace)
{
    QObject::connect(&subspace, &win::subspace::x11DesktopNumberChanged, this, [this, &subspace]() {
        auto const data = get_subspace_data(subspace);
        Q_EMIT desktopDataChanged(data.id, data);
        Q_EMIT desktopsChanged(desktops());
    });
    QObject::connect(&subspace, &win::subspace::nameChanged, this, [this, &subspace]() {
        auto const data = get_subspace_data(subspace);
        Q_EMIT desktopDataChanged(data.id, data);
        Q_EMIT desktopsChanged(desktops());
    });

    auto const data = get_subspace_data(subspace);
    Q_EMIT desktopCreated(data.id, data);
    Q_EMIT desktopsChanged(desktops());
}

subspace_data subspace_manager_wrap::get_subspace_data(win::subspace& subspace)
{
    return {
        .position = subspace.x11DesktopNumber() - 1,
        .id = subspace.id(),
        .name = subspace.name(),
    };
}

}
