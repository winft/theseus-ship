/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/utils.h"
#include "input/touch.h"

#include <config-kwin.h>

extern "C" {
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_touch.h>
}

namespace KWin::input::backend::wlroots
{
class platform;

class touch : public input::touch
{
    Q_OBJECT
public:
    using er = base::event_receiver<touch>;

    wlr_touch* backend{nullptr};

    touch(wlr_input_device* dev, platform* plat);
    touch(touch const&) = delete;
    touch& operator=(touch const&) = delete;
    touch(touch&& other) noexcept = default;
    touch& operator=(touch&& other) noexcept = default;
    ~touch() = default;

private:
    er destroyed;
    er down_rec;
    er up_rec;
    er motion_rec;
    er cancel_rec;
#if HAVE_WLR_TOUCH_FRAME
    er frame_rec;
#endif
};

}
