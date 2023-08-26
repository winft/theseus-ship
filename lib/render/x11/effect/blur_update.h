/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <render/effect/interface/effect_integration.h>
#include <render/effect/interface/effect_window.h>
#include <render/effect/internal_win_update.h>

#include <xcb/xcb.h>

namespace KWin::render::x11
{

template<typename EffectIntegrator>
effect::region_update get_blur_update(EffectIntegrator& effi, EffectWindow& window)
{
    if (auto internal_upd = get_internal_window_blur_update(effi, window);
        internal_upd.base.window) {
        return internal_upd;
    }

    if (effi.atom == XCB_ATOM_NONE) {
        return {};
    }

    effect::region_update upd;
    upd.base.window = &window;

    auto const value = window.readProperty(effi.atom, XCB_ATOM_CARDINAL, 32);
    if (value.isNull()) {
        // Property was removed. Inform with an invalid update.
        upd.base.valid = false;
        return upd;
    }

    if (value.size() > 0 && !(value.size() % (4 * sizeof(uint32_t)))) {
        auto cardinals = reinterpret_cast<const uint32_t*>(value.constData());
        for (unsigned int i = 0; i < value.size() / sizeof(uint32_t);) {
            int x = cardinals[i++];
            int y = cardinals[i++];
            int w = cardinals[i++];
            int h = cardinals[i++];
            upd.value += QRect(x, y, w, h);
        }
    }
    return upd;
}

}
