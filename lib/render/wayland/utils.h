/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/scene.h"

namespace KWin::render::wayland
{

template<typename Win>
auto max_coverage_output(Win* window) -> typename Win::space_t::base_t::output_t*
{
    auto const enabled_outputs = window->space.base.outputs;
    if (enabled_outputs.empty()) {
        return nullptr;
    }

    auto max_out = enabled_outputs[0];
    int max_area = 0;

    auto const geo = win::visible_rect(window);

    for (auto out : enabled_outputs) {
        auto const intersect_geo = geo.intersected(out->geometry());
        auto const area = intersect_geo.width() * intersect_geo.height();

        if (area > max_area) {
            max_area = area;
            max_out = out;
        }
    }

    return max_out;
}

}
