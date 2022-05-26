/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/platform.h"
#include "input/platform.h"
#include "input/types.h"

#include <QPointF>

namespace Wrapland::Server
{
class virtual_keyboard_manager_v1;
}

namespace KWin
{

namespace base::wayland
{
class platform;
}

namespace input
{

class dpms_filter;

namespace wayland
{

class input_method;

class KWIN_EXPORT platform : public input::platform
{
    Q_OBJECT
public:
    platform(base::wayland::platform const& base);
    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    ~platform() override;

    void install_shortcuts();
    void update_keyboard_leds(input::keyboard_leds leds);

    void toggle_touchpads();
    void enable_touchpads();
    void disable_touchpads();

    void start_interactive_window_selection(std::function<void(KWin::Toplevel*)> callback,
                                            QByteArray const& cursorName = QByteArray()) override;
    void start_interactive_position_selection(std::function<void(QPoint const&)> callback) override;

    void turn_outputs_on();
    void warp_pointer(QPointF const& pos, uint32_t time);

    std::unique_ptr<wayland::input_method> input_method;
    std::unique_ptr<Wrapland::Server::virtual_keyboard_manager_v1> virtual_keyboard;
    std::unique_ptr<input::dpms_filter> dpms_filter;

private:
    void setup_touchpad_shortcuts();

    base::wayland::platform const& base;
    bool touchpads_enabled{true};
};

KWIN_EXPORT void add_dbus(input::platform* platform);

}
}
}
