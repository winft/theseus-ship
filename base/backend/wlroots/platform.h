/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/utils.h"
#include "base/wayland/platform.h"

#include <kwin_export.h>

#include <functional>
#include <memory>

extern "C" {
#include <wlr/backend.h>
#include <wlr/backend/drm.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
#include <wlr/util/log.h>
}

namespace Wrapland::Server
{
class Display;
}

namespace KWin::base::backend::wlroots
{

inline wlr_backend* get_backend(wlr_backend* backend, std::function<bool(wlr_backend*)> check)
{
    if (!wlr_backend_is_multi(backend)) {
        return check(backend) ? backend : nullptr;
    }

    struct check_data {
        decltype(check) fct;
        wlr_backend* backend{nullptr};
    } data;
    data.fct = check;

    auto check_backend = [](wlr_backend* backend, void* data) {
        auto check = static_cast<check_data*>(data);
        if (check->fct(backend)) {
            check->backend = backend;
        }
    };
    wlr_multi_for_each_backend(backend, check_backend, &data);
    return data.backend;
}

inline wlr_backend* get_drm_backend(wlr_backend* backend)
{
    return get_backend(backend, wlr_backend_is_drm);
}

inline wlr_backend* get_headless_backend(wlr_backend* backend)
{
    return get_backend(backend, wlr_backend_is_headless);
}

class KWIN_EXPORT platform : public base::wayland::platform
{
public:
    wlr_backend* backend{nullptr};

    platform() = default;
    explicit platform(Wrapland::Server::Display* display);
    explicit platform(wlr_backend* backend);

    platform(platform const&) = delete;
    platform& operator=(platform const&) = delete;
    platform(platform&& other) noexcept;
    platform& operator=(platform&& other) noexcept;
    ~platform() override;

    wlr_session* session() const;
    clockid_t get_clockid() const override;

private:
    std::unique_ptr<event_receiver<platform>> destroyed;
    std::unique_ptr<event_receiver<platform>> new_output;
};

}
