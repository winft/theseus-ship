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

subspace_manager::subspace_manager(win::subspace_manager* parent)
    : QObject(parent->qobject.get())
    , m_manager(parent)
{
    qDBusRegisterMetaType<KWin::win::dbus::subspace_data>();
    qDBusRegisterMetaType<KWin::win::dbus::subspace_data_vector>();

    new VirtualDesktopManagerAdaptor(this);
    QDBusConnection::sessionBus().registerObject(
        QStringLiteral("/VirtualDesktopManager"),
        QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
        this);

    QObject::connect(m_manager->qobject.get(),
                     &win::subspace_manager_qobject::current_changed,
                     this,
                     [this](auto /*prev*/, auto next) { Q_EMIT currentChanged(next->id()); });

    QObject::connect(m_manager->qobject.get(),
                     &win::subspace_manager_qobject::countChanged,
                     this,
                     [this](uint previousCount, uint newCount) {
                         Q_UNUSED(previousCount);
                         Q_EMIT countChanged(newCount);
                         Q_EMIT desktopsChanged(desktops());
                     });

    QObject::connect(
        m_manager->qobject.get(),
        &win::subspace_manager_qobject::navigationWrappingAroundChanged,
        this,
        [this]() { Q_EMIT navigationWrappingAroundChanged(isNavigationWrappingAround()); });

    QObject::connect(m_manager->qobject.get(),
                     &win::subspace_manager_qobject::rowsChanged,
                     this,
                     &subspace_manager::rowsChanged);

    for (auto&& vd : m_manager->subspaces) {
        QObject::connect(vd, &win::subspace::x11DesktopNumberChanged, this, [this, vd]() {
            subspace_data data{
                .position = vd->x11DesktopNumber() - 1, .id = vd->id(), .name = vd->name()};
            Q_EMIT desktopDataChanged(vd->id(), data);
            Q_EMIT desktopsChanged(desktops());
        });
        QObject::connect(vd, &win::subspace::nameChanged, this, [this, vd]() {
            subspace_data data{
                .position = vd->x11DesktopNumber() - 1, .id = vd->id(), .name = vd->name()};
            Q_EMIT desktopDataChanged(vd->id(), data);
            Q_EMIT desktopsChanged(desktops());
        });
    }
    QObject::connect(
        m_manager->qobject.get(),
        &win::subspace_manager_qobject::subspace_created,
        this,
        [this](auto vd) {
            QObject::connect(vd, &win::subspace::x11DesktopNumberChanged, this, [this, vd]() {
                subspace_data data{
                    .position = vd->x11DesktopNumber() - 1, .id = vd->id(), .name = vd->name()};
                Q_EMIT desktopDataChanged(vd->id(), data);
                Q_EMIT desktopsChanged(desktops());
            });
            QObject::connect(vd, &win::subspace::nameChanged, this, [this, vd]() {
                subspace_data data{
                    .position = vd->x11DesktopNumber() - 1, .id = vd->id(), .name = vd->name()};
                Q_EMIT desktopDataChanged(vd->id(), data);
                Q_EMIT desktopsChanged(desktops());
            });
            subspace_data data{
                .position = vd->x11DesktopNumber() - 1, .id = vd->id(), .name = vd->name()};
            Q_EMIT desktopCreated(vd->id(), data);
            Q_EMIT desktopsChanged(desktops());
        });
    QObject::connect(m_manager->qobject.get(),
                     &win::subspace_manager_qobject::subspace_removed,
                     this,
                     [this](auto vd) {
                         Q_EMIT desktopRemoved(vd->id());
                         Q_EMIT desktopsChanged(desktops());
                     });
}

uint subspace_manager::count() const
{
    return m_manager->subspaces.size();
}

void subspace_manager::setRows(uint rows)
{
    if (static_cast<uint>(m_manager->grid().height()) == rows) {
        return;
    }

    m_manager->setRows(rows);
    m_manager->save();
}

uint subspace_manager::rows() const
{
    return m_manager->rows();
}

void subspace_manager::setCurrent(const QString& id)
{
    if (m_manager->current->id() == id) {
        return;
    }

    auto vd = m_manager->subspace_for_id(id);
    if (vd) {
        m_manager->setCurrent(vd);
    }
}

QString subspace_manager::current() const
{
    return m_manager->current->id();
}

void subspace_manager::setNavigationWrappingAround(bool wraps)
{
    if (m_manager->isNavigationWrappingAround() == wraps) {
        return;
    }

    m_manager->setNavigationWrappingAround(wraps);
}

bool subspace_manager::isNavigationWrappingAround() const
{
    return m_manager->isNavigationWrappingAround();
}

subspace_data_vector subspace_manager::desktops() const
{
    auto const& subs = m_manager->subspaces;
    subspace_data_vector desktopVect;
    desktopVect.reserve(m_manager->subspaces.size());

    std::transform(subs.cbegin(), subs.cend(), std::back_inserter(desktopVect), [](auto vd) {
        return subspace_data{
            .position = vd->x11DesktopNumber() - 1, .id = vd->id(), .name = vd->name()};
    });

    return desktopVect;
}

void subspace_manager::createDesktop(uint position, const QString& name)
{
    m_manager->create_subspace(position, name);
}

void subspace_manager::setDesktopName(const QString& id, const QString& name)
{
    auto vd = m_manager->subspace_for_id(id);
    if (!vd) {
        return;
    }
    if (vd->name() == name) {
        return;
    }

    vd->setName(name);
    m_manager->save();
}

void subspace_manager::removeDesktop(const QString& id)
{
    m_manager->remove_subspace(id);
}

}
