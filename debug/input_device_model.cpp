/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "input_device_model.h"

#include "input/dbus/device.h"
#include "input/dbus/device_manager.h"
#include "input/wayland/platform.h"
#include "input/wayland/redirect.h"
#include "main.h"

#include <QMetaProperty>

namespace KWin::debug
{

static const quint32 s_propertyBitMask = 0xFFFF0000;
static const quint32 s_clientBitMask = 0x0000FFFF;

input_device_model::input_device_model(QObject* parent)
    : QAbstractItemModel(parent)
{
    auto& platform
        = static_cast<input::wayland::redirect*>(kwinApp()->input->redirect.get())->platform;

    for (auto& dev : platform->dbus->devices) {
        m_devices.push_back(dev);
    }

    for (auto& dev : m_devices) {
        setupDeviceConnections(dev);
    }

    QObject::connect(platform->dbus.get(),
                     &input::dbus::device_manager::deviceAdded,
                     this,
                     [this](auto const& sys_name) {
                         auto redirect = static_cast<input::wayland::redirect*>(
                             kwinApp()->input->redirect.get());
                         for (auto& dev : redirect->platform->dbus->devices) {
                             if (dev->sysName() != sys_name) {
                                 continue;
                             }
                             beginInsertRows(QModelIndex(), m_devices.count(), m_devices.count());
                             m_devices << dev;
                             setupDeviceConnections(dev);
                             endInsertRows();
                             return;
                         }
                     });

    QObject::connect(platform->dbus.get(),
                     &input::dbus::device_manager::deviceRemoved,
                     this,
                     [this](auto const& sys_name) {
                         int index{-1};
                         for (int i = 0; i < m_devices.size(); i++) {
                             if (m_devices.at(i)->sysName() == sys_name) {
                                 index = i;
                                 break;
                             }
                         }
                         if (index == -1) {
                             return;
                         }
                         beginRemoveRows(QModelIndex(), index, index);
                         m_devices.removeAt(index);
                         endRemoveRows();
                     });
}

int input_device_model::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return 2;
}

QVariant input_device_model::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }
    if (!index.parent().isValid() && index.column() == 0) {
        if (index.row() >= m_devices.count()) {
            return QVariant();
        }
        if (role == Qt::DisplayRole) {
            return m_devices.at(index.row())->name();
        }
    }
    if (index.parent().isValid()) {
        if (role == Qt::DisplayRole) {
            const auto device = m_devices.at(index.parent().row());
            const auto property = device->metaObject()->property(index.row());
            if (index.column() == 0) {
                return property.name();
            } else if (index.column() == 1) {
                return device->property(property.name());
            }
        }
    }
    return QVariant();
}

QModelIndex input_device_model::index(int row, int column, const QModelIndex& parent) const
{
    if (column >= 2) {
        return QModelIndex();
    }
    if (parent.isValid()) {
        if (parent.internalId() & s_propertyBitMask) {
            return QModelIndex();
        }
        if (row >= m_devices.at(parent.row())->metaObject()->propertyCount()) {
            return QModelIndex();
        }
        return createIndex(row, column, quint32(row + 1) << 16 | parent.internalId());
    }
    if (row >= m_devices.count()) {
        return QModelIndex();
    }
    return createIndex(row, column, row + 1);
}

int input_device_model::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid()) {
        return m_devices.count();
    }
    if (parent.internalId() & s_propertyBitMask) {
        return 0;
    }

    return m_devices.at(parent.row())->metaObject()->propertyCount();
}

QModelIndex input_device_model::parent(const QModelIndex& child) const
{
    if (child.internalId() & s_propertyBitMask) {
        const quintptr parentId = child.internalId() & s_clientBitMask;
        return createIndex(parentId - 1, 0, parentId);
    }
    return QModelIndex();
}

void input_device_model::setupDeviceConnections(input::dbus::device* device)
{
    QObject::connect(device->dev, &input::control::device::enabled_changed, this, [this, device] {
        const QModelIndex parent = index(m_devices.indexOf(device), 0, QModelIndex());
        const QModelIndex child
            = index(device->metaObject()->indexOfProperty("enabled"), 1, parent);
        Q_EMIT dataChanged(child, child, QVector<int>{Qt::DisplayRole});
    });
    if (auto& ctrl = device->pointer_ctrl) {
        QObject::connect(ctrl, &input::control::pointer::left_handed_changed, this, [this, device] {
            const QModelIndex parent = index(m_devices.indexOf(device), 0, QModelIndex());
            const QModelIndex child
                = index(device->metaObject()->indexOfProperty("leftHanded"), 1, parent);
            Q_EMIT dataChanged(child, child, QVector<int>{Qt::DisplayRole});
        });
        QObject::connect(
            ctrl, &input::control::pointer::acceleration_changed, this, [this, device] {
                const QModelIndex parent = index(m_devices.indexOf(device), 0, QModelIndex());
                const QModelIndex child = index(
                    device->metaObject()->indexOfProperty("pointerAcceleration"), 1, parent);
                Q_EMIT dataChanged(child, child, QVector<int>{Qt::DisplayRole});
            });
    };
}

}
