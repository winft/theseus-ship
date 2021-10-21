/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/platform.h"

namespace KWin::input
{

namespace dbus
{
class device_manager;
}

class cursor;
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
    platform() = default;
    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    platform(platform&& other) noexcept = default;
    platform& operator=(platform&& other) noexcept = default;
    ~platform() override = default;

    void start_interactive_window_selection(std::function<void(KWin::Toplevel*)> callback,
                                            QByteArray const& cursorName = QByteArray()) override;
    void start_interactive_position_selection(std::function<void(QPoint const&)> callback) override;
};

KWIN_EXPORT void add_dbus(input::platform* platform);

}
}
