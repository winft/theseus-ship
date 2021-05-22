/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/pointer.h"
#include "platform/utils.h"

extern "C" {
#include <wlr/types/wlr_pointer.h>
}

namespace KWin::input::backend::wlroots
{
class platform;

class pointer : public input::pointer
{
    Q_OBJECT
public:
    using er = event_receiver<pointer>;

    wlr_pointer* backend{nullptr};

    pointer(wlr_input_device* dev, platform* plat);
    pointer(pointer const&) = delete;
    pointer& operator=(pointer const&) = delete;
    pointer(pointer&& other) noexcept = default;
    pointer& operator=(pointer&& other) noexcept = default;
    ~pointer() = default;

private:
    er destroyed;
    er motion_rec;
    er motion_absolute_rec;
    er button_rec;
    er axis_rec;
    er frame;
    er swipe_begin;
    er swipe_update;
    er swipe_end;
    er pinch_begin;
    er pinch_update;
    er pinch_end;
};

}
