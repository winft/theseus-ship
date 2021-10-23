/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/platform.h"

#include "base/platform.h"

namespace KWin
{

namespace base
{
namespace backend
{
class wlroots;
}
template<typename Backend, typename Output>
class platform;
}

class AbstractWaylandOutput;

using wayland_base = base::platform<base::backend::wlroots, AbstractWaylandOutput>;

namespace input
{

namespace dbus
{
class device_manager;
}

class cursor;
class dpms_filter;

class keyboard;
class pointer;
class redirect;
class switch_device;
class touch;

namespace wayland
{

class platform : public input::platform
{
    Q_OBJECT
public:
    platform(wayland_base const& base);
    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    platform(platform&& other) noexcept = default;
    platform& operator=(platform&& other) noexcept = default;
    ~platform() override;

    void update_keyboard_leds(input::xkb::LEDs leds);

    void toggle_touchpads();
    void enable_touchpads();
    void disable_touchpads();

    void start_interactive_window_selection(std::function<void(KWin::Toplevel*)> callback,
                                            QByteArray const& cursorName = QByteArray()) override;
    void start_interactive_position_selection(std::function<void(QPoint const&)> callback) override;

    void turn_outputs_on();

    std::unique_ptr<input::dpms_filter> dpms_filter;

private:
    wayland_base const& base;
    bool touchpads_enabled{true};
};

KWIN_EXPORT void add_dbus(input::platform* platform);

}
}
}
