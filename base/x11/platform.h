/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/output.h"
#include "base/platform.h"

#include <vector>

namespace KWin::base::x11
{

class platform : public base::platform
{
public:
    std::vector<output*> outputs;

    std::vector<base::output*> get_outputs() const override
    {
        return outputs;
    }
};

}
