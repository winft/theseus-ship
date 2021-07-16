/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "wlr_includes.h"

extern "C" {
#include <wlr/backend/headless.h>
#include <wlr/backend/multi.h>
}

namespace KWin::render::backend::wlroots
{

template<typename Output>
int rotation_in_degree(Output* out)
{
    switch (out->native->transform) {
    case WL_OUTPUT_TRANSFORM_NORMAL:
    case WL_OUTPUT_TRANSFORM_FLIPPED:
        return 0;
    case WL_OUTPUT_TRANSFORM_90:
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
        return 90;
    case WL_OUTPUT_TRANSFORM_180:
    case WL_OUTPUT_TRANSFORM_FLIPPED_180:
        return 180;
    case WL_OUTPUT_TRANSFORM_270:
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:
        return 270;
    }
    Q_UNREACHABLE();
    return 0;
}

template<typename Output>
bool has_portrait_transform(Output* out)
{
    auto const& transform = out->transform();
    return transform == Output::Transform::Rotated90 || transform == Output::Transform::Rotated270
        || transform == Output::Transform::Flipped90 || transform == Output::Transform::Flipped270;
}

inline bool is_headless_backend(wlr_backend* backend)
{
    if (!wlr_backend_is_multi(backend)) {
        return wlr_backend_is_headless(backend);
    }

    auto is_headless{false};
    auto check_backend = [](wlr_backend* backend, void* data) {
        auto is_headless = static_cast<bool*>(data);
        if (wlr_backend_is_headless(backend)) {
            *is_headless = true;
        }
    };
    wlr_multi_for_each_backend(backend, check_backend, &is_headless);
    return is_headless;
}

}
