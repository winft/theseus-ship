/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/xkb.h"

#include <kwin_export.h>

#include <KSharedConfig>
#include <QObject>
#include <memory>
#include <vector>

namespace KWin::input
{

namespace dbus
{
class device_manager;
}

class keyboard;
class pointer;
class switch_device;
class touch;

class KWIN_EXPORT platform : public QObject
{
    Q_OBJECT
public:
    std::vector<keyboard*> keyboards;
    std::vector<pointer*> pointers;
    std::vector<switch_device*> switches;
    std::vector<touch*> touchs;

    std::unique_ptr<dbus::device_manager> dbus;
    KSharedConfigPtr config;

    bool touchpads_enabled{true};

    platform();
    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    platform(platform&& other) noexcept = default;
    platform& operator=(platform&& other) noexcept = default;
    ~platform();

    void update_keyboard_leds(input::xkb::LEDs leds);

    void toggle_touchpads();
    void enable_touchpads();
    void disable_touchpads();

Q_SIGNALS:
    void keyboard_added(KWin::input::keyboard*);
    void pointer_added(KWin::input::pointer*);
    void switch_added(KWin::input::switch_device*);
    void touch_added(KWin::input::touch*);

    void keyboard_removed(KWin::input::keyboard*);
    void pointer_removed(KWin::input::pointer*);
    void switch_removed(KWin::input::switch_device*);
    void touch_removed(KWin::input::touch*);
};

}
