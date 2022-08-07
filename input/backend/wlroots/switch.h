/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/utils.h"
#include "input/switch.h"

extern "C" {
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_switch.h>
}

namespace KWin::input::backend::wlroots
{

class switch_device : public input::switch_device
{
    Q_OBJECT
public:
    using er = base::event_receiver<switch_device>;

    switch_device(wlr_input_device* dev, input::platform* platform);
    switch_device(switch_device const&) = delete;
    switch_device& operator=(switch_device const&) = delete;
    ~switch_device() override = default;

private:
    er destroyed;
    er toggle_rec;
};

}
