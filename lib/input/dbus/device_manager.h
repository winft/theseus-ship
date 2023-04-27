/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "device_helpers.h"

#include "input/platform_qobject.h"
#include "kwin_export.h"

#include <QObject>
#include <QStringList>
#include <vector>

namespace KWin::input::dbus
{
class device;

class KWIN_EXPORT device_manager_qobject : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.InputDeviceManager")
    Q_PROPERTY(QStringList devicesSysNames READ devicesSysNames CONSTANT)

public:
    device_manager_qobject();
    ~device_manager_qobject() override;

    QStringList devicesSysNames();

    std::vector<device*> devices;

Q_SIGNALS:
    void deviceAdded(QString sysName);
    void deviceRemoved(QString sysName);
};

template<typename Platform>
class device_manager
{
public:
    explicit device_manager(Platform& platform)
        : qobject{std::make_unique<device_manager_qobject>()}
        , platform{platform}
    {
        QObject::connect(platform.qobject.get(),
                         &platform_qobject::keyboard_added,
                         qobject.get(),
                         [this](auto dev) { add_device(dev, this); });
        QObject::connect(platform.qobject.get(),
                         &platform_qobject::pointer_added,
                         qobject.get(),
                         [this](auto dev) { add_device(dev, this); });
        QObject::connect(platform.qobject.get(),
                         &platform_qobject::switch_added,
                         qobject.get(),
                         [this](auto dev) { add_device(dev, this); });
        QObject::connect(platform.qobject.get(),
                         &platform_qobject::touch_added,
                         qobject.get(),
                         [this](auto dev) { add_device(dev, this); });

        QObject::connect(platform.qobject.get(),
                         &platform_qobject::keyboard_removed,
                         qobject.get(),
                         [this](auto dev) { remove_device(dev, this); });
        QObject::connect(platform.qobject.get(),
                         &platform_qobject::pointer_removed,
                         qobject.get(),
                         [this](auto dev) { remove_device(dev, this); });
        QObject::connect(platform.qobject.get(),
                         &platform_qobject::switch_removed,
                         qobject.get(),
                         [this](auto dev) { remove_device(dev, this); });
        QObject::connect(platform.qobject.get(),
                         &platform_qobject::touch_removed,
                         qobject.get(),
                         [this](auto dev) { remove_device(dev, this); });
    }

    std::unique_ptr<device_manager_qobject> qobject;

private:
    Platform& platform;
};

}
