/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

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

    input->dpms_filter = std::make_unique<dpms_filter<Input, typename Input::redirect_t>>(
        *input, *input->redirect);
    input->redirect->prependInputEventFilter(input->dpms_filter.get());
}

}
