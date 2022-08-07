/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/utils.h"
#include "input/platform.h"
#include "input/pointer.h"

extern "C" {
#include <wlr/types/wlr_pointer.h>
}

namespace KWin::input::backend::wlroots
{

class pointer : public input::pointer
{
    Q_OBJECT
public:
    using er = base::event_receiver<pointer>;

    pointer(wlr_input_device* dev, input::platform* platform);
    pointer(pointer const&) = delete;
    pointer& operator=(pointer const&) = delete;
    ~pointer() override = default;

    input::platform* platform;

private:
    er destroyed;
    er motion_rec;
    er motion_absolute_rec;
    er button_rec;
    er axis_rec;
    er frame_rec;
    er swipe_begin_rec;
    er swipe_update_rec;
    er swipe_end_rec;
    er pinch_begin_rec;
    er pinch_update_rec;
    er pinch_end_rec;
};

}
