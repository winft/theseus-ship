/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "device_manager.h"

#include "device.h"

#include "input/control/keyboard.h"
#include "input/control/pointer.h"
#include "input/control/touch.h"
#include "input/keyboard.h"
#include "input/platform.h"
#include "input/pointer.h"
#include "input/touch.h"

#include "utils.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <algorithm>

namespace KWin::input::dbus
{

template<typename Dev, typename Target, typename... Ctrl>
bool add_to_devices(Dev dev, Target& target, Ctrl const&... cmp)
{
    static_assert((!std::is_same_v<Target, Ctrl> && ...),
                  "target and cmps must have differnt types.");

    auto check_add_device = [&dev, &target](auto cmp) {
        if (cmp && cmp->metadata.sys_name == dev->control->metadata.sys_name) {
            target = dev->control;
            return true;
        }
        return false;
    };

    return (check_add_device(cmp) || ...);
}

// Helper to allow dependent static assert in the else case.
template<typename T>
inline constexpr bool always_false_v = false;

// Some devices are for example pointer + keyboard. We advertise them only once.
template<typename Dev, typename Manager>
bool check_existing_devices(Dev dev, Manager manager)
{
    static_assert(std::is_pointer<decltype(dev)>::value, "dev must have pointer type.");

    for (auto dbus_dev : manager->devices) {
        auto& kc = dbus_dev->keyboard_ctrl;
        auto& pc = dbus_dev->pointer_ctrl;
        auto& tc = dbus_dev->touch_ctrl;

        if constexpr (std::is_same_v<decltype(dev), input::keyboard*>) {
            if (add_to_devices(dev, kc, pc, tc)) {
                return true;
            }
        } else if constexpr (std::is_same_v<decltype(dev), input::pointer*>) {
            if (add_to_devices(dev, pc, kc, tc)) {
                return true;
            }
        } else if constexpr (std::is_same_v<decltype(dev), input::touch*>) {
            if (add_to_devices(dev, tc, kc, pc)) {
                return true;
            }
        } else {
            static_assert(always_false_v<dev>, "Should not be reached.");
        }
    }

    return false;
}

template<typename Dev, typename Manager>
void add_device(Dev dev, Manager manager)
{
    if (!dev->control) {
        return;
    }
    if (check_existing_devices(dev, manager)) {
        return;
    }

    auto sys_name = dev->control->metadata.sys_name;
    manager->devices.push_back(new device(dev->control, manager));

    Q_EMIT manager->deviceAdded(sys_name.c_str());
}

template<typename Dev, typename Dbus_dev>
bool remove_from_devices(Dev dev, Dbus_dev dbus_dev)
{
    static_assert(std::is_pointer<decltype(dev)>::value, "dev must have pointer type.");
    static_assert(std::is_pointer<decltype(dbus_dev)>::value, "dbus_dev must have pointer type.");

    auto& kc = dbus_dev->keyboard_ctrl;
    auto& pc = dbus_dev->pointer_ctrl;
    auto& tc = dbus_dev->touch_ctrl;

    if constexpr (std::is_same_v<decltype(dev), input::keyboard*>) {
        if (kc == dev->control) {
            kc = nullptr;
            return !(pc || tc);
        }
    } else if constexpr (std::is_same_v<decltype(dev), input::pointer*>) {
        if (pc == dev->control) {
            pc = nullptr;
            return !(kc || tc);
        }
    } else if constexpr (std::is_same_v<decltype(dev), input::touch*>) {
        if (tc == dev->control) {
            tc = nullptr;
            return !(kc || pc);
        }
    } else {
        static_assert(always_false_v<dev>, "Should not be reached.");
    }

    return false;
}

template<typename Dev, typename Manager>
void remove_device(Dev dev, Manager manager)
{
    if (!dev->control) {
        return;
    }
    auto sys_name = dev->control->metadata.sys_name;
    auto& devices = manager->devices;

    auto const old_size = devices.size();

    devices.erase(
        std::remove_if(devices.begin(),
                       devices.end(),
                       [&dev](auto& dbus_dev) { return remove_from_devices(dev, dbus_dev); }),
        devices.end());

    if (old_size != devices.size()) {
        Q_EMIT manager->deviceRemoved(sys_name.c_str());
    }
}

device_manager::device_manager(platform* plat)
    : plat{plat}
{
    connect(plat, &platform::keyboard_added, this, [this](auto dev) { add_device(dev, this); });
    connect(plat, &platform::pointer_added, this, [this](auto dev) { add_device(dev, this); });
    connect(plat, &platform::touch_added, this, [this](auto dev) { add_device(dev, this); });

    connect(
        plat, &platform::keyboard_removed, this, [this](auto dev) { remove_device(dev, this); });
    connect(plat, &platform::pointer_removed, this, [this](auto dev) { remove_device(dev, this); });
    connect(plat, &platform::touch_removed, this, [this](auto dev) { remove_device(dev, this); });

    QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/kde/KWin/InputDevice"),
                                                 QStringLiteral("org.kde.KWin.InputDeviceManager"),
                                                 this,
                                                 QDBusConnection::ExportAllProperties
                                                     | QDBusConnection::ExportAllSignals);
}

device_manager::~device_manager()
{
    QDBusConnection::sessionBus().unregisterObject(
        QStringLiteral("/org/kde/KWin/InputDeviceManager"));
}

QStringList device_manager::devicesSysNames()
{
    QStringList ret;
    for (auto device : devices) {
        ret << device->sys_name.c_str();
    }
    return ret;
}

}
