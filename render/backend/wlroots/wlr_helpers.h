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
        auto fmt = set->formats[fmt_index];
        Format format;
        format.format = fmt->format;
        for (size_t mod_index = 0; mod_index < fmt->len; mod_index++) {
            format.modifiers.insert(fmt->modifiers[mod_index]);
        }
        formats.push_back(std::move(format));
    }

    return formats;
}

}
