/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "output.h"

#include "base/singleton_interface.h"
#include "input/wayland/platform.h"
#include <base/backend/wlroots/backend.h>
#include <base/platform_qobject.h>
#include <base/wayland/platform_helpers.h>
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

template<typename Mod>
class xwl_platform;

struct xwl_platform_mod {
    using platform_t = base::wayland::xwl_platform<xwl_platform_mod>;
    using render_t = render::wayland::xwl_platform<platform_t>;
    using input_t = input::wayland::platform<platform_t>;
    using space_t = win::wayland::xwl_space<platform_t>;

    std::unique_ptr<render_t> render;
    std::unique_ptr<input_t> input;
    std::unique_ptr<space_t> space;
    std::unique_ptr<xwl::xwayland<space_t>> xwayland;
};

template<typename Mod = xwl_platform_mod>
class xwl_platform
{
public:
    using type = xwl_platform<Mod>;
    using qobject_t = platform_qobject;
    using backend_t = backend::wlroots::backend<type>;
    using output_t = output<type>;

    using render_t = typename Mod::render_t;
    using input_t = typename Mod::input_t;
    using space_t = typename Mod::space_t;

    xwl_platform(base::config config,
                 std::string const& socket_name,
                 base::wayland::start_options flags,
                 backend::wlroots::start_options options)
        : qobject{std::make_unique<platform_qobject>([this] { return topology.max_scale; })}
        , config{std::move(config)}
        , server{std::make_unique<wayland::server<type>>(*this, socket_name, flags)}
        , backend{*this, options}
        , x11_event_filters{std::make_unique<base::x11::event_filter_manager>()}
    {
        wayland::platform_init(*this);
    }

    xwl_platform(type const&) = delete;
    xwl_platform& operator=(type const&) = delete;
    xwl_platform(type&& other) = delete;
    xwl_platform& operator=(type&& other) = delete;

    virtual ~xwl_platform()
    {
        platform_cleanup(*this);
        singleton_interface::get_outputs = {};
    }

    std::unique_ptr<platform_qobject> qobject;
    base::operation_mode operation_mode;
    output_topology topology;
    base::config config;
    base::x11::data x11_data;
    std::unique_ptr<base::options> options;

    std::unique_ptr<wayland::server<type>> server;
    std::unique_ptr<Wrapland::Server::drm_lease_device_v1> drm_lease_device;

    // All outputs, including disabled ones.
    std::vector<output_t*> all_outputs;

    // Enabled outputs only, so outputs that are relevant for our compositing.
    std::vector<output_t*> outputs;
    std::unique_ptr<base::seat::session> session;
    backend_t backend;
    QProcessEnvironment process_environment;

    std::unique_ptr<x11::event_filter_manager> x11_event_filters;

    Mod mod;
};

}
