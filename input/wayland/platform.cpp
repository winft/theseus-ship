/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "platform.h"

#include "input/dbus/device_manager.h"

namespace KWin::input::wayland
{

void platform::start_interactive_window_selection(std::function<void(KWin::Toplevel*)> callback,
                                                  QByteArray const& cursorName)
{
    if (!redirect) {
        callback(nullptr);
        return;
    }
    redirect->startInteractiveWindowSelection(callback, cursorName);
}

void platform::start_interactive_position_selection(std::function<void(QPoint const&)> callback)
{
    if (!redirect) {
        callback(QPoint(-1, -1));
        return;
    }
    redirect->startInteractivePositionSelection(callback);
}

void add_dbus(input::platform* platform)
{
    platform->dbus = std::make_unique<dbus::device_manager>(platform);
}

}
