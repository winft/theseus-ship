/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/utils.h"
#include "input/keyboard.h"
#include "input/platform.h"

extern "C" {
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
}

namespace KWin::input::backend::wlroots
{

class keyboard : public input::keyboard
{
    Q_OBJECT
public:
    using er = base::event_receiver<keyboard>;

    wlr_keyboard* backend{nullptr};

    keyboard(wlr_input_device* dev, input::platform* platform);
    keyboard(keyboard const&) = delete;
    keyboard& operator=(keyboard const&) = delete;
    ~keyboard() override = default;

    input::platform* platform;

private:
    er destroyed;
    er key_rec;
    er modifiers_rec;
};

}
