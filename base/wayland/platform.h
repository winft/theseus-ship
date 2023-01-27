/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "output.h"

#include "base/platform.h"
#include "base/singleton_interface.h"
#include "input/wayland/platform.h"
#include "render/wayland/platform.h"
#include "utils/algorithm.h"
#include "win/wayland/space.h"
#include "xwl/xwayland.h"

#include <Wrapland/Server/drm_lease_v1.h>
#include <cassert>
#include <memory>
#include <vector>

namespace KWin::base::wayland
{

class platform : public base::platform
{
public:
    using output_t = output<platform>;
    using render_t = render::wayland::platform<platform>;
    using input_t = input::wayland::platform<platform>;
    using space_t = win::wayland::space<platform>;

    platform(base::config config)
        : base::platform(std::move(config))
    {
    }

    platform(base::config config,
             std::string const& socket_name,
             base::wayland::start_options flags)
        : base::platform(std::move(config))
        , server{std::make_unique<wayland::server<platform>>(*this, socket_name, flags)}
    {
    }

    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    platform(platform&& other) = delete;
    platform& operator=(platform&& other) = delete;

    void enable_output(output_t* output)
    {
        assert(!contains(outputs, output));
        outputs.push_back(output);
        Q_EMIT output_added(output);
    }

    void disable_output(output_t* output)
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

    std::unique_ptr<wayland::server<platform>> server;
    std::unique_ptr<Wrapland::Server::drm_lease_device_v1> drm_lease_device;

    // All outputs, including disabled ones.
    std::vector<output_t*> all_outputs;

    // Enabled outputs only, so outputs that are relevant for our compositing.
    std::vector<output_t*> outputs;

    std::unique_ptr<render_t> render;
    std::unique_ptr<input_t> input;
    std::unique_ptr<space_t> space;
    std::unique_ptr<xwl::xwayland<win::wayland::space<platform>>> xwayland;
};

}
