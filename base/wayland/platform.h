/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "output.h"

#include "base/platform.h"
#include "utils/algorithm.h"

#include <cassert>
#include <memory>
#include <vector>

namespace Wrapland::Server
{
class drm_lease_device_v1;
}

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
    }

    void disable_output(base::wayland::output* output)
    {
        assert(contains(outputs, output));
        remove_all(outputs, output);
        Q_EMIT output_removed(output);
    }

    std::vector<base::output*> get_outputs() const override
    {
        std::vector<base::output*> vec;
        for (auto&& output : outputs) {
            vec.push_back(output);
        }
        return vec;
    }

    // A Wayland DRM node
    std::unique_ptr<Wrapland::Server::drm_lease_device_v1> drm_lease_device;
};

}
