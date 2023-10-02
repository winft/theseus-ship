/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

extern "C" {
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_input_device.h>
}

namespace KWin::input::backend::wlroots
{

template<typename Dev>
libinput_device* get_libinput_device(Dev dev)
{
    auto casted_dev = reinterpret_cast<wlr_input_device*>(dev);
    if (wlr_input_device_is_libinput(casted_dev)) {
        return wlr_libinput_get_device_handle(casted_dev);
    }
    return nullptr;
}

}
