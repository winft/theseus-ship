/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QObject>
#include <QStringList>

#include <memory>
#include <vector>

namespace KWin::input
{
class platform;

namespace dbus
{
class device;

class KWIN_EXPORT device_manager : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.InputDeviceManager")
    Q_PROPERTY(QStringList devicesSysNames READ devicesSysNames CONSTANT)

private:
    platform* plat;

public:
    explicit device_manager(platform* plat);
    ~device_manager() override;

    QStringList devicesSysNames();

    std::vector<device*> devices;

Q_SIGNALS:
    void deviceAdded(QString sysName);
    void deviceRemoved(QString sysName);
};

}
}
