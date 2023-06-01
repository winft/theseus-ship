/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "drm_lease.h"
#include "output.h"
#include "platform_helpers.h"

#include "base/utils.h"
#include "base/wayland/platform.h"
#include "kwin_export.h"
#include "utils/flags.h"

#include <functional>
#include <memory>

namespace Wrapland::Server
{
class Display;
}

namespace KWin::base::backend::wlroots
{

class non_desktop_output;

class KWIN_EXPORT platform : public base::wayland::platform
{
public:
    using abstract_type = base::wayland::platform;
    using output_t = wlroots::output;

    platform(base::config config,
             std::string const& socket_name,
             base::wayland::start_options flags,
             start_options options);

    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    platform(platform&& other) = delete;
    platform& operator=(platform&& other) = delete;
    ~platform() override;

    clockid_t get_clockid() const override;

    std::vector<std::unique_ptr<drm_lease>> leases;
    std::vector<non_desktop_output*> non_desktop_outputs;

    wlr_backend* backend{nullptr};
    wlr_session* wlroots_session{nullptr};

private:
    void init();
    void setup_drm_leasing(Wrapland::Server::Display* display, wlr_backend* drm_backend);

    std::unique_ptr<event_receiver<platform>> destroyed;
    std::unique_ptr<event_receiver<platform>> new_output;
};

}
