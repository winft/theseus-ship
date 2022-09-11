/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/filters/dpms.h"

#include <memory>

namespace KWin::input::wayland
{

template<typename Redirect>
void create_dpms_filter(Redirect& redirect)
{
    if (redirect.dpms_filter) {
        // Already another output is off.
        return;
    }

    redirect.dpms_filter = std::make_unique<dpms_filter<Redirect>>(redirect);
    redirect.prependInputEventFilter(redirect.dpms_filter.get());
}

}
