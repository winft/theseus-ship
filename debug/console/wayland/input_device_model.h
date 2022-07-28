/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QAbstractItemModel>

namespace KWin
{

namespace input::dbus
{
class device;
class device_manager;
}

namespace debug
{

class input_device_model : public QAbstractItemModel
{
    Q_OBJECT
public:
    explicit input_device_model(input::dbus::device_manager& dbus, QObject* parent = nullptr);

    int columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    int rowCount(const QModelIndex& parent) const override;
    QModelIndex parent(const QModelIndex& child) const override;

private:
    void setupDeviceConnections(input::dbus::device* device);
    QVector<input::dbus::device*> m_devices;
};

}
}
