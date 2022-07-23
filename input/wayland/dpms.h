/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "redirect.h"

#include "input/filters/dpms.h"

#include <memory>

namespace KWin::input::wayland
{

template<typename Input>
void create_dpms_filter(Input* input)
{
    if (input->dpms_filter) {
        // Already another output is off.
        return;
    }

    auto& wlredirect = static_cast<redirect&>(*input->redirect);
    input->dpms_filter = std::make_unique<dpms_filter<Input, redirect>>(*input, wlredirect);
    wlredirect.prependInputEventFilter(input->dpms_filter.get());
}

}
