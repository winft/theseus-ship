/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "dbus/device_manager.h"
#include "global_shortcuts_manager.h"
#include "keyboard.h"
#include "platform_qobject.h"
#include "singleton_interface.h"
#include "xkb/manager.h"

#include "utils/algorithm.h"

#include <KSharedConfig>
#include <QAction>
#include <QObject>
#include <memory>
#include <vector>

namespace KWin::input
{

template<typename Keys, typename Platform>
void platform_add_keyboard(Keys* keys, Platform& platform)
{
    platform.keyboards.push_back(keys);
    Q_EMIT platform.qobject->keyboard_added(keys);
}

template<typename Pointer, typename Platform>
void platform_add_pointer(Pointer* pointer, Platform& platform)
{
    platform.pointers.push_back(pointer);
    Q_EMIT platform.qobject->pointer_added(pointer);
}

template<typename Switch, typename Platform>
void platform_add_switch(Switch* switch_dev, Platform& platform)
{
    platform.switches.push_back(switch_dev);
    Q_EMIT platform.qobject->switch_added(switch_dev);
}

template<typename Touch, typename Platform>
void platform_add_touch(Touch* touch, Platform& platform)
{
    platform.touchs.push_back(touch);
    Q_EMIT platform.qobject->touch_added(touch);
}

template<typename Keys, typename Platform>
void platform_remove_keyboard(Keys* keys, Platform& platform)
{
    remove_all(platform.keyboards, keys);
    Q_EMIT platform.qobject->keyboard_removed(keys);
}

template<typename Pointer, typename Platform>
void platform_remove_pointer(Pointer* pointer, Platform& platform)
{
    remove_all(platform.pointers, pointer);
    Q_EMIT platform.qobject->pointer_removed(pointer);
}

template<typename Switch, typename Platform>
void platform_remove_switch(Switch* switch_dev, Platform& platform)
{
    remove_all(platform.switches, switch_dev);
    Q_EMIT platform.qobject->switch_removed(switch_dev);
}

template<typename Touch, typename Platform>
void platform_remove_touch(Touch* touch, Platform& platform)
{
    remove_all(platform.touchs, touch);
    Q_EMIT platform.qobject->touch_removed(touch);
}

template<typename Base>
class platform
{
public:
    using base_t = Base;

    platform(Base& base, KSharedConfigPtr config)
        : qobject{std::make_unique<platform_qobject>(
            [this](auto accel) { registerGlobalAccel(accel); })}
        , base{base}
        , config{config}
    {
        qRegisterMetaType<button_state>();
        qRegisterMetaType<key_state>();
    }

    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    virtual ~platform() = default;

    void registerPointerShortcut(Qt::KeyboardModifiers modifiers,
                                 Qt::MouseButton pointerButtons,
                                 QAction* action)
    {
        shortcuts->registerPointerShortcut(action, modifiers, pointerButtons);
    }

    void registerAxisShortcut(Qt::KeyboardModifiers modifiers,
                              PointerAxisDirection axis,
                              QAction* action)
    {
        shortcuts->registerAxisShortcut(action, modifiers, axis);
    }

    void registerTouchpadSwipeShortcut(SwipeDirection direction, QAction* action)
    {
        shortcuts->registerTouchpadSwipe(action, direction);
    }

    void registerGlobalAccel(KGlobalAccelInterface* interface)
    {
        shortcuts->setKGlobalAccelInterface(interface);
    }

    std::unique_ptr<platform_qobject> qobject;
    Base& base;

    std::vector<keyboard*> keyboards;
    std::vector<pointer*> pointers;
    std::vector<switch_device*> switches;
    std::vector<touch*> touchs;

    std::unique_ptr<global_shortcuts_manager> shortcuts;

    KSharedConfigPtr config;
};

}
