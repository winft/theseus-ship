/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "output.h"

#include "base/platform.h"
#include "base/singleton_interface.h"
#include "input/wayland/platform.h"
#include "script/platform.h"
#include <base/platform_helpers.h>
#include <base/x11/data.h>
#include <base/x11/event_filter_manager.h>
#include <render/wayland/xwl_platform.h>
#include <win/wayland/xwl_space.h>
#include <xwl/xwayland.h>

#include <QProcessEnvironment>
#include <Wrapland/Server/drm_lease_v1.h>
#include <cassert>
#include <memory>
#include <vector>

namespace KWin::base::wayland
{

class xwl_platform : public base::platform
{
public:
    using output_t = output<xwl_platform>;
    using render_t = render::wayland::xwl_platform<xwl_platform>;
    using input_t = input::wayland::platform<xwl_platform>;
    using space_t = win::wayland::xwl_space<render_t, input_t>;

    xwl_platform(base::config config,
                 std::string const& socket_name,
                 base::wayland::start_options flags)
        : config{std::move(config)}
        , x11_event_filters{std::make_unique<base::x11::event_filter_manager>()}
        , server{std::make_unique<wayland::server<xwl_platform>>(*this, socket_name, flags)}
    {
        platform_init(*this);
    }

    xwl_platform(xwl_platform const&) = delete;
    xwl_platform& operator=(xwl_platform const&) = delete;
    xwl_platform(xwl_platform&& other) = delete;
    xwl_platform& operator=(xwl_platform&& other) = delete;

    ~xwl_platform() override
    {
        singleton_interface::get_outputs = {};
    }

    base::operation_mode operation_mode;
    base::config config;
    base::x11::data x11_data;

    std::unique_ptr<base::options> options;
    std::unique_ptr<base::seat::session> session;
    std::unique_ptr<x11::event_filter_manager> x11_event_filters;

    QProcessEnvironment process_environment;

    std::unique_ptr<wayland::server<xwl_platform>> server;

    std::unique_ptr<Wrapland::Server::drm_lease_device_v1> drm_lease_device;

    // All outputs, including disabled ones.
    std::vector<output_t*> all_outputs;

    // Enabled outputs only, so outputs that are relevant for our compositing.
    std::vector<output_t*> outputs;

    std::unique_ptr<render_t> render;
    std::unique_ptr<input_t> input;
    std::unique_ptr<space_t> space;
    std::unique_ptr<scripting::platform<space_t>> script;
    std::unique_ptr<xwl::xwayland<space_t>> xwayland;
};

}
