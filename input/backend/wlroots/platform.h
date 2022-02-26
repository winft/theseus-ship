/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/backend/wlroots/platform.h"
#include "input/wayland/platform.h"

#include "kwin_export.h"

extern "C" {
#include <wlr/backend/libinput.h>
#include <wlr/backend/multi.h>
}

namespace KWin::input::backend::wlroots
{

template<typename Dev>
inline libinput_device* get_libinput_device(Dev dev)
{
    auto casted_dev = reinterpret_cast<wlr_input_device*>(dev);
    if (wlr_input_device_is_libinput(casted_dev)) {
        return wlr_libinput_get_device_handle(casted_dev);
    }
    return nullptr;
}

class KWIN_EXPORT platform : public input::wayland::platform
{
    Q_OBJECT
public:
    platform(base::wayland::platform const& base);
    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    ~platform() override = default;

    base::backend::wlroots::platform const& base;

private:
    base::event_receiver<platform> add_device;
};

}
