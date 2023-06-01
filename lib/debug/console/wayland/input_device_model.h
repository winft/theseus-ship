/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/dbus/device.h"

#include <QAbstractItemModel>
#include <type_traits>

namespace KWin::debug
{

class KWIN_EXPORT input_device_model : public QAbstractItemModel
{
    Q_OBJECT
public:
    explicit input_device_model(QObject* parent = nullptr);

    int columnCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    int rowCount(const QModelIndex& parent) const override;
    QModelIndex parent(const QModelIndex& child) const override;

    void begin_insert_rows(QModelIndex const& parent, int first, int last);
    void end_insert_rows();
    void begin_remove_rows(QModelIndex const& parent, int first, int last);
    void end_remove_rows();

    void setupDeviceConnections(input::dbus::device* device);

    QVector<input::dbus::device*> m_devices;
};

template<typename Source>
void setup_input_device_model(input_device_model& model, Source& source)
{
    for (auto& dev : source.qobject->devices) {
        model.m_devices.push_back(dev);
    }

    for (auto& dev : model.m_devices) {
        model.setupDeviceConnections(dev);
    }

    auto sender = source.qobject.get();
    using sender_t = typename decltype(source.qobject)::element_type;

    QObject::connect(
        sender, &sender_t::deviceAdded, &model, [&source, &model](auto const& sys_name) {
            for (auto& dev : source.qobject->devices) {
                if (dev->sysName() != sys_name) {
                    continue;
                }
                model.begin_insert_rows(
                    QModelIndex(), model.m_devices.count(), model.m_devices.count());
                model.m_devices << dev;
                model.setupDeviceConnections(dev);
                model.end_insert_rows();
                return;
            }
        });

    QObject::connect(sender, &sender_t::deviceRemoved, &model, [&model](auto const& sys_name) {
        int index{-1};
        for (int i = 0; i < model.m_devices.size(); i++) {
            if (model.m_devices.at(i)->sysName() == sys_name) {
                index = i;
                break;
            }
        }
        if (index == -1) {
            return;
        }
        model.begin_remove_rows(QModelIndex(), index, index);
        model.m_devices.removeAt(index);
        model.end_remove_rows();
    });
}

}
