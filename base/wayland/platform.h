/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "output.h"

#include "base/platform.h"
#include "screens.h"
#include "utils/algorithm.h"

#include <cassert>
#include <vector>

namespace KWin::base::wayland
{

class platform : public base::platform
{
public:
    // All outputs, including disabled ones.
    std::vector<output*> all_outputs;

    // Enabled outputs only, so outputs that are relevant for our compositing.
    std::vector<output*> outputs;

    void enable_output(base::wayland::output* output)
    {
        assert(!contains(outputs, output));
        outputs.push_back(output);
        Q_EMIT output_added(output);
        screens.updateAll();
    }

    void disable_output(base::wayland::output* output)
    {
        assert(contains(outputs, output));
        remove_all(outputs, output);
        Q_EMIT output_removed(output);
        screens.updateAll();
    }

    std::vector<base::output*> get_outputs() const override
    {
        std::vector<base::output*> vec;
        for (auto&& output : outputs) {
            vec.push_back(output);
        }
        return vec;
    }
};

}
