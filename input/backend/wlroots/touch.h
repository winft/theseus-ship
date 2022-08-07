/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/utils.h"
#include "input/platform.h"
#include "input/touch.h"

extern "C" {
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_touch.h>
}

namespace KWin::input::backend::wlroots
{

class touch : public input::touch
{
    Q_OBJECT
public:
    using er = base::event_receiver<touch>;

    touch(wlr_input_device* dev, input::platform* plat);
    touch(touch const&) = delete;
    touch& operator=(touch const&) = delete;
    ~touch() override = default;

    input::platform* platform;

private:
    er destroyed;
    er down_rec;
    er up_rec;
    er motion_rec;
    er cancel_rec;
    er frame_rec;
};

}
