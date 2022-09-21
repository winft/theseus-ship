/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QRegion>
#include <vector>

namespace KWin::win
{

template<typename Output>
struct window_render_data {
    // Relative to client geometry.
    QRegion damage_region;

    // Relative to frame geometry.
    QRegion repaints_region;
    QRegion layer_repaints_region;

    // Area to be opaque. Only provides valuable information if hasAlpha is @c true.
    QRegion opaque_region;

    // Records all outputs that still need to be repainted for the current repaint regions.
    std::vector<Output*> repaint_outputs;

    int bit_depth{24};
    bool ready_for_painting{false};
    bool is_damaged{false};
};

}
