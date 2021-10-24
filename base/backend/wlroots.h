/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/utils.h"

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

namespace KWin::base
{

namespace wayland
{
class output;
}

namespace backend
{

inline wlr_backend* wlroots_get_backend(wlr_backend* backend,
                                        std::function<bool(wlr_backend*)> check)
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

inline wlr_backend* wlroots_get_drm_backend(wlr_backend* backend)
{
    return wlroots_get_backend(backend, wlr_backend_is_drm);
}

inline wlr_backend* wlroots_get_headless_backend(wlr_backend* backend)
{
    return wlroots_get_backend(backend, wlr_backend_is_headless);
}

class KWIN_EXPORT wlroots
{
public:
    using output = base::wayland::output;

    wlr_backend* backend{nullptr};

    // TODO(romangg): remove all but one ctor once the startup sequence is cleaned up.
    wlroots();
    explicit wlroots(Wrapland::Server::Display* display);
    explicit wlroots(wlr_backend* backend);
    void init(wlr_backend* backend);

    wlroots(wlroots const&) = delete;
    wlroots& operator=(wlroots const&) = delete;
    wlroots(wlroots&& other) noexcept;
    wlroots& operator=(wlroots&& other) noexcept;
    ~wlroots();

    wlr_session* session() const
    {
        return wlr_backend_get_session(backend);
    }

private:
    std::unique_ptr<event_receiver<wlroots>> destroyed;
};

}
}
