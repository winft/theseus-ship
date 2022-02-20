/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/platform.h"

#include "config-kwin.h"

#include <memory>

namespace KWin::input::x11
{

class window_selector;
class xinput_integration;

class platform : public input::platform
{
    Q_OBJECT
public:
    platform();
    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    ~platform() override;

    void setup_action_for_global_accel(QAction* action) override;

    void start_interactive_window_selection(std::function<void(KWin::Toplevel*)> callback,
                                            QByteArray const& cursorName = QByteArray()) override;
    void start_interactive_position_selection(std::function<void(QPoint const&)> callback) override;

#if HAVE_X11_XINPUT
    std::unique_ptr<xinput_integration> xinput;
#endif
    std::unique_ptr<window_selector> window_sel;

private:
    void create_cursor();
};

}
