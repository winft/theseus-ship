/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control/switch.h"
#include "pointer.h"

#include <kwin_export.h>

#include <QObject>

namespace KWin::input
{

class platform;
class switch_device;

enum class switch_type {
    lid = 1,
    tablet_mode,
};

enum class switch_state {
    off = 0,
    on,
    toggle,
};

struct toggle_event {
    switch_type type;
    switch_state state;
    event<switch_device> base;
};

class KWIN_EXPORT switch_device : public QObject
{
    Q_OBJECT
public:
    input::platform* plat;
    control::switch_device* control{nullptr};

    switch_device(platform* plat, QObject* parent = nullptr);
    switch_device(switch_device const&) = delete;
    switch_device& operator=(switch_device const&) = delete;
    switch_device(switch_device&& other) noexcept = default;
    switch_device& operator=(switch_device&& other) noexcept = default;
    ~switch_device() = default;

Q_SIGNALS:
    void toggle(toggle_event);
};

}
