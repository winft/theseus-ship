/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "config-kwin.h"

#include "base/wayland/output_transform.h"
#include "wlr_includes.h"

namespace KWin::render::backend::wlroots
{

inline int rotation_in_degree(base::wayland::output_transform transform)
{
    using Tr = base::wayland::output_transform;

    switch (transform) {
    case Tr::normal:
    case Tr::flipped:
        return 0;
    case Tr::rotated_90:
    case Tr::flipped_90:
        return 90;
    case Tr::rotated_180:
    case Tr::flipped_180:
        return 180;
    case Tr::rotated_270:
    case Tr::flipped_270:
        return 270;
    }
    Q_UNREACHABLE();
    return 0;
}

template<typename Output>
base::wayland::output_transform get_transform(Output&& out)
{
    using Tr = base::wayland::output_transform;

    switch (out.native->transform) {
    case WL_OUTPUT_TRANSFORM_NORMAL:
        return Tr::normal;
    case WL_OUTPUT_TRANSFORM_FLIPPED:
        return Tr::flipped;
    case WL_OUTPUT_TRANSFORM_90:
        return Tr::rotated_90;
    case WL_OUTPUT_TRANSFORM_FLIPPED_90:
        return Tr::flipped_90;
    case WL_OUTPUT_TRANSFORM_180:
        return Tr::rotated_180;
    case WL_OUTPUT_TRANSFORM_FLIPPED_180:
        return Tr::flipped_180;
    case WL_OUTPUT_TRANSFORM_270:
        return Tr::rotated_270;
    case WL_OUTPUT_TRANSFORM_FLIPPED_270:
        return Tr::flipped_270;
    }
    Q_UNREACHABLE();
    return Tr::normal;
}

template<typename Output>
bool has_portrait_transform(Output&& out)
{
    using Tr = base::wayland::output_transform;
    auto const& transform = out.transform();
    return transform == Tr::rotated_90 || transform == Tr::rotated_270
        || transform == Tr::flipped_90 || transform == Tr::flipped_270;
}

template<typename Region>
pixman_region32_t create_scaled_pixman_region(Region const& src_region, int scale)
{
    pixman_region32_t region;
    std::vector<pixman_box32> boxes;

    for (auto it = src_region.cbegin(); it != src_region.cend(); it++) {
        boxes.push_back({it->left() * scale,
                         it->top() * scale,
                         (it->right() + 1) * scale,
                         (it->bottom() + 1) * scale});
    }

    pixman_region32_init_rects(&region, boxes.data(), boxes.size());
    return region;
}

template<typename Region>
pixman_region32_t create_pixman_region(Region const& src_region)
{
    return create_scaled_pixman_region(src_region, 1);
}

template<typename Format>
std::vector<Format> get_drm_formats(wlr_drm_format_set const* set)
{
    if (!set) {
        return {};
    }

    std::vector<Format> formats;

    for (size_t fmt_index = 0; fmt_index < set->len; fmt_index++) {
#if HAVE_WLR_VALUE_DRM_FORMATS
        auto fmt = set->formats[fmt_index];
#else
        auto fmt = *set->formats[fmt_index];
#endif
        Format format;
        format.format = fmt.format;
        for (size_t mod_index = 0; mod_index < fmt.len; mod_index++) {
            format.modifiers.insert(fmt.modifiers[mod_index]);
        }
        formats.push_back(std::move(format));
    }

    return formats;
}

}
