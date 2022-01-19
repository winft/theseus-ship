/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "virtual_desktop_manager.h"

#include "virtualdesktopmanageradaptor.h"

#include "win/virtual_desktops.h"

namespace KWin::win::dbus
{

virtual_desktop_manager::virtual_desktop_manager(win::virtual_desktop_manager* parent)
    : QObject(parent)
    , m_manager(parent)
{
    qDBusRegisterMetaType<KWin::win::dbus::virtual_desktop_data>();
    qDBusRegisterMetaType<KWin::win::dbus::virtual_desktop_data_vector>();

    new VirtualDesktopManagerAdaptor(this);
    QDBusConnection::sessionBus().registerObject(
        QStringLiteral("/VirtualDesktopManager"),
        QStringLiteral("org.kde.KWin.VirtualDesktopManager"),
        this);

    QObject::connect(m_manager,
                     &win::virtual_desktop_manager::currentChanged,
                     this,
                     [this](uint previousDesktop, uint newDesktop) {
                         Q_UNUSED(previousDesktop);
                         Q_UNUSED(newDesktop);
                         Q_EMIT currentChanged(m_manager->currentDesktop()->id());
                     });

    QObject::connect(m_manager,
                     &win::virtual_desktop_manager::countChanged,
                     this,
                     [this](uint previousCount, uint newCount) {
                         Q_UNUSED(previousCount);
                         Q_EMIT countChanged(newCount);
                         Q_EMIT desktopsChanged(desktops());
                     });

    QObject::connect(
        m_manager, &win::virtual_desktop_manager::navigationWrappingAroundChanged, this, [this]() {
            Q_EMIT navigationWrappingAroundChanged(isNavigationWrappingAround());
        });

    QObject::connect(m_manager,
                     &win::virtual_desktop_manager::rowsChanged,
                     this,
                     &virtual_desktop_manager::rowsChanged);

    for (auto vd : m_manager->desktops()) {
        QObject::connect(vd, &win::virtual_desktop::x11DesktopNumberChanged, this, [this, vd]() {
            virtual_desktop_data data{
                .position = vd->x11DesktopNumber() - 1, .id = vd->id(), .name = vd->name()};
            Q_EMIT desktopDataChanged(vd->id(), data);
            Q_EMIT desktopsChanged(desktops());
        });
        QObject::connect(vd, &win::virtual_desktop::nameChanged, this, [this, vd]() {
            virtual_desktop_data data{
                .position = vd->x11DesktopNumber() - 1, .id = vd->id(), .name = vd->name()};
            Q_EMIT desktopDataChanged(vd->id(), data);
            Q_EMIT desktopsChanged(desktops());
        });
    }
    QObject::connect(
        m_manager, &win::virtual_desktop_manager::desktopCreated, this, [this](auto vd) {
            QObject::connect(
                vd, &win::virtual_desktop::x11DesktopNumberChanged, this, [this, vd]() {
                    virtual_desktop_data data{
                        .position = vd->x11DesktopNumber() - 1, .id = vd->id(), .name = vd->name()};
                    Q_EMIT desktopDataChanged(vd->id(), data);
                    Q_EMIT desktopsChanged(desktops());
                });
            QObject::connect(vd, &win::virtual_desktop::nameChanged, this, [this, vd]() {
                virtual_desktop_data data{
                    .position = vd->x11DesktopNumber() - 1, .id = vd->id(), .name = vd->name()};
                Q_EMIT desktopDataChanged(vd->id(), data);
                Q_EMIT desktopsChanged(desktops());
            });
            virtual_desktop_data data{
                .position = vd->x11DesktopNumber() - 1, .id = vd->id(), .name = vd->name()};
            Q_EMIT desktopCreated(vd->id(), data);
            Q_EMIT desktopsChanged(desktops());
        });
    QObject::connect(
        m_manager, &win::virtual_desktop_manager::desktopRemoved, this, [this](auto vd) {
            Q_EMIT desktopRemoved(vd->id());
            Q_EMIT desktopsChanged(desktops());
        });
}

uint virtual_desktop_manager::count() const
{
    return m_manager->count();
}

void virtual_desktop_manager::setRows(uint rows)
{
    if (static_cast<uint>(m_manager->grid().height()) == rows) {
        return;
    }

    m_manager->setRows(rows);
    m_manager->save();
}

uint virtual_desktop_manager::rows() const
{
    return m_manager->rows();
}

void virtual_desktop_manager::setCurrent(const QString& id)
{
    if (m_manager->currentDesktop()->id() == id) {
        return;
    }

    auto vd = m_manager->desktopForId(id.toUtf8());
    if (vd) {
        m_manager->setCurrent(vd);
    }
}

QString virtual_desktop_manager::current() const
{
    return m_manager->currentDesktop()->id();
}

void virtual_desktop_manager::setNavigationWrappingAround(bool wraps)
{
    if (m_manager->isNavigationWrappingAround() == wraps) {
        return;
    }

    m_manager->setNavigationWrappingAround(wraps);
}

bool virtual_desktop_manager::isNavigationWrappingAround() const
{
    return m_manager->isNavigationWrappingAround();
}

virtual_desktop_data_vector virtual_desktop_manager::desktops() const
{
    auto const desks = m_manager->desktops();
    virtual_desktop_data_vector desktopVect;
    desktopVect.reserve(m_manager->count());

    std::transform(
        desks.constBegin(), desks.constEnd(), std::back_inserter(desktopVect), [](auto vd) {
            return virtual_desktop_data{
                .position = vd->x11DesktopNumber() - 1, .id = vd->id(), .name = vd->name()};
        });

    return desktopVect;
}

void virtual_desktop_manager::createDesktop(uint position, const QString& name)
{
    m_manager->createVirtualDesktop(position, name);
}

void virtual_desktop_manager::setDesktopName(const QString& id, const QString& name)
{
    auto vd = m_manager->desktopForId(id.toUtf8());
    if (!vd) {
        return;
    }
    if (vd->name() == name) {
        return;
    }

    vd->setName(name);
    m_manager->save();
}

void virtual_desktop_manager::removeDesktop(const QString& id)
{
    m_manager->removeVirtualDesktop(id.toUtf8());
}

}
