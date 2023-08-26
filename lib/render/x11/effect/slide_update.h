/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/effect/internal_win_update.h"

#include <render/effect/interface/effect_integration.h>
#include <render/effect/interface/effect_window.h>

#include <xcb/xcb.h>

namespace KWin::render::x11
{

template<typename EffectIntegrator>
effect::anim_update get_slide_update(EffectIntegrator& effi, EffectWindow& window)
{
    if (auto internal_upd = get_internal_window_slide_update(effi, window);
        internal_upd.base.window) {
        return internal_upd;
    }

    if (effi.atom == XCB_ATOM_NONE) {
        return {};
    }

    // _KDE_SLIDE atom format(each field is an uint32_t):
    // <offset> <location> [<slide in duration>] [<slide out duration>] [<slide length>]
    //
    // If offset is equal to -1, this effect will decide what offset to use
    // given edge of the screen, from which the window has to slide.
    //
    // If slide in duration is equal to 0 milliseconds, the default slide in
    // duration will be used. Same with the slide out duration.
    //
    // NOTE: If only slide in duration has been provided, then it will be
    // also used as slide out duration. I.e. if you provided only slide in
    // duration, then slide in duration == slide out duration.

    auto const value = window.readProperty(effi.atom, effi.atom, 32);

    if (value.isEmpty()) {
        // We inform that the property was removed by sending an invalid update on the window.
        return {{&window, false}, {}, {}, {}, {}, {}};
    }

    // Offset and location are required.
    if (static_cast<size_t>(value.size()) < sizeof(uint32_t) * 2) {
        return {};
    }

    auto value_data = reinterpret_cast<const uint32_t*>(value.data());

    // Per convention offset might be -1 to indicate the effect should choose. So first cast to int.
    auto offset_as_int = static_cast<int>(value_data[0]);
    auto offset = static_cast<double>(offset_as_int);

    auto pos{effect::position::center};
    switch (value_data[1]) {
    case 0: // West
        pos = effect::position::left;
        break;
    case 1: // North
        pos = effect::position::top;
        break;
    case 2: // East
        pos = effect::position::right;
        break;
    case 3: // South
    default:
        pos = effect::position::bottom;
        break;
    }

    if (pos == effect::position::center) {
        // Invalid position value.
        return {};
    }

    std::chrono::milliseconds in{};
    std::chrono::milliseconds out{};
    if (static_cast<size_t>(value.size()) >= sizeof(uint32_t) * 3) {
        in = out = std::chrono::milliseconds(value_data[2]);
        if (static_cast<size_t>(value.size()) >= sizeof(uint32_t) * 4) {
            out = std::chrono::milliseconds(value_data[3]);
        }
    }

    double distance{0};
    if (static_cast<size_t>(value.size()) >= sizeof(uint32_t) * 5) {
        distance = value_data[4];
    }

    return {{&window, true}, pos, in, out, offset, distance};
}

}
