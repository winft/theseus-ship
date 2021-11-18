/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/platform.h"
#include "input/types.h"

#include "base/platform.h"

namespace Wrapland::Server
{
class virtual_keyboard_manager_v1;
}

namespace KWin
{

namespace base
{
namespace backend
{
class wlroots;
}

template<typename Backend>
class platform;
}

using wayland_base = base::platform<base::backend::wlroots>;

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
class input_method;

class KWIN_EXPORT platform : public input::platform
{
    Q_OBJECT
public:
    platform(wayland_base const& base);
    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    ~platform() override;

    void update_keyboard_leds(input::keyboard_leds leds);

    void toggle_touchpads();
    void enable_touchpads();
    void disable_touchpads();

    void start_interactive_window_selection(std::function<void(KWin::Toplevel*)> callback,
                                            QByteArray const& cursorName = QByteArray()) override;
    void start_interactive_position_selection(std::function<void(QPoint const&)> callback) override;

    void turn_outputs_on();

    std::unique_ptr<wayland::input_method> input_method;
    std::unique_ptr<Wrapland::Server::virtual_keyboard_manager_v1> virtual_keyboard;
    std::unique_ptr<input::dpms_filter> dpms_filter;

private:
    wayland_base const& base;
    bool touchpads_enabled{true};
};

KWIN_EXPORT void add_dbus(input::platform* platform);

}
}
}
