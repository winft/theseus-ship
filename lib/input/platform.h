/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "config.h"
#include "keyboard.h"
#include "platform_qobject.h"
#include "singleton_interface.h"
#include "xkb/manager.h"

#include "utils/algorithm.h"

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

}
