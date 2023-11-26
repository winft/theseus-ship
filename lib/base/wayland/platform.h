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
#include "script/platform.h"
#include <base/platform_helpers.h>
#include <win/wayland/space.h>

#include <QProcessEnvironment>
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
    using space_t = win::wayland::space<render_t, input_t>;

    platform(base::config config,
             std::string const& socket_name,
             base::wayland::start_options flags)
        : config{std::move(config)}
        , server{std::make_unique<wayland::server<platform>>(*this, socket_name, flags)}
    {
        platform_init(*this);
    }

    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    platform(platform&& other) = delete;
    platform& operator=(platform&& other) = delete;

    ~platform() override
    {
        singleton_interface::get_outputs = {};
    }

    base::operation_mode operation_mode;
    base::config config;

    std::unique_ptr<base::options> options;
    std::unique_ptr<base::seat::session> session;

    QProcessEnvironment process_environment;

    std::unique_ptr<wayland::server<platform>> server;

    std::unique_ptr<Wrapland::Server::drm_lease_device_v1> drm_lease_device;

    // All outputs, including disabled ones.
    std::vector<output_t*> all_outputs;

    // Enabled outputs only, so outputs that are relevant for our compositing.
    std::vector<output_t*> outputs;

    std::unique_ptr<render_t> render;
    std::unique_ptr<input_t> input;
    std::unique_ptr<space_t> space;
    std::unique_ptr<scripting::platform<space_t>> script;
};

}
