/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"
#include "base/x11/platform.h"

#include <memory>
#include <vector>

namespace KWin::base::backend::x11
{

class platform : public base::x11::platform
{
public:
    void update_outputs();

private:
    template<typename Resources>
    void update_outputs_impl();

    std::unique_ptr<base::x11::event_filter> randr_filter;
};

}
