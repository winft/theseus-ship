/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/wayland/output_transform.h"
#include "wlr_includes.h"

namespace KWin::render::backend::wlroots
{

template<typename Output>
int rotation_in_degree(Output&& out)
{
    switch (out.native->transform) {
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
bool has_portrait_transform(Output&& out)
{
    using Tr = base::wayland::output_transform;
    auto const& transform = out.transform();
    return transform == Tr::rotated_90 || transform == Tr::rotated_270
        || transform == Tr::flipped_90 || transform == Tr::flipped_270;
}

}
