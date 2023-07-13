/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "utils/flags.h"

extern "C" {
#define static
#include <wlr/backend.h>
#include <wlr/backend/drm.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
#undef static
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

enum class start_options {
    none = 0x0,
    headless = 0x1,
};

}

ENUM_FLAGS(KWin::base::backend::wlroots::start_options)
