/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "device.h"

#include "input/control/keyboard.h"
#include "input/control/pointer.h"
#include "input/control/switch.h"
#include "input/control/touch.h"
#include "input/keyboard.h"
#include "input/platform.h"
#include "input/pointer.h"
#include "input/switch.h"
#include "input/touch.h"

namespace KWin::input::dbus
{

template<typename Dev, typename Target, typename... Ctrl>
bool add_to_devices(Dev dev, Target& target, Ctrl const&... cmp)
{
    static_assert((!std::is_same_v<Target, Ctrl> && ...),
                  "target and cmps must have differnt types.");

    auto check_add_device = [&dev, &target](auto cmp) {
        if (cmp && cmp->metadata.sys_name == dev->control->metadata.sys_name) {
            target = dev->control.get();
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

    for (auto dbus_dev : manager->qobject->devices) {
        auto& kc = dbus_dev->keyboard_ctrl;
        auto& pc = dbus_dev->pointer_ctrl;
        auto& sc = dbus_dev->switch_ctrl;
        auto& tc = dbus_dev->touch_ctrl;

        if constexpr (std::is_same_v<decltype(dev), input::keyboard*>) {
            if (add_to_devices(dev, kc, pc, sc, tc)) {
                return true;
            }
        } else if constexpr (std::is_same_v<decltype(dev), input::pointer*>) {
            if (add_to_devices(dev, pc, kc, sc, tc)) {
                return true;
            }
        } else if constexpr (std::is_same_v<decltype(dev), input::switch_device*>) {
            if (add_to_devices(dev, sc, kc, pc, tc)) {
                return true;
            }
        } else if constexpr (std::is_same_v<decltype(dev), input::touch*>) {
            if (add_to_devices(dev, tc, kc, pc, sc)) {
                return true;
            }
        } else {
            static_assert(always_false_v<Dev>, "Should not be reached.");
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
    manager->qobject->devices.push_back(new device(dev->control.get(), manager->qobject.get()));

    Q_EMIT manager->qobject->deviceAdded(sys_name.c_str());
}

template<typename Dev, typename Dbus_dev>
bool remove_from_devices(Dev dev, Dbus_dev dbus_dev)
{
    static_assert(std::is_pointer<decltype(dev)>::value, "dev must have pointer type.");
    static_assert(std::is_pointer<decltype(dbus_dev)>::value, "dbus_dev must have pointer type.");

    auto& kc = dbus_dev->keyboard_ctrl;
    auto& pc = dbus_dev->pointer_ctrl;
    auto& sc = dbus_dev->switch_ctrl;
    auto& tc = dbus_dev->touch_ctrl;
    auto dev_ctrl = dev->control.get();

    if constexpr (std::is_same_v<decltype(dev), input::keyboard*>) {
        if (kc == dev_ctrl) {
            kc = nullptr;
            return !(pc || sc || tc);
        }
    } else if constexpr (std::is_same_v<decltype(dev), input::pointer*>) {
        if (pc == dev_ctrl) {
            pc = nullptr;
            return !(kc || sc || tc);
        }
    } else if constexpr (std::is_same_v<decltype(dev), input::switch_device*>) {
        if (sc == dev_ctrl) {
            sc = nullptr;
            return !(kc || pc || tc);
        }
    } else if constexpr (std::is_same_v<decltype(dev), input::touch*>) {
        if (tc == dev_ctrl) {
            tc = nullptr;
            return !(kc || pc || sc);
        }
    } else {
        static_assert(always_false_v<Dev>, "Should not be reached.");
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
    auto& devices = manager->qobject->devices;

    dbus::device* dbus_device{nullptr};

    devices.erase(std::remove_if(devices.begin(),
                                 devices.end(),
                                 [&dev, &dbus_device](auto& dbus_dev) {
                                     if (remove_from_devices(dev, dbus_dev)) {
                                         dbus_device = dbus_dev;
                                         return true;
                                     }
                                     return false;
                                 }),
                  devices.end());

    if (dbus_device) {
        Q_EMIT manager->qobject->deviceRemoved(sys_name.c_str());
        delete dbus_device;
    }
}

}
