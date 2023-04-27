/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/effect/internal_win_update.h"

#include <kwineffects/effect_integration.h>
#include <kwineffects/effect_window.h>

#include <xcb/xcb.h>

namespace KWin::render::x11
{

template<typename EffectIntegrator>
effect::color_update get_contrast_update(EffectIntegrator& effi, EffectWindow& window)
{
    if (auto internal_upd = get_internal_window_contrast_update(effi, window);
        internal_upd.base.window) {
        return internal_upd;
    }

    if (effi.atom == XCB_ATOM_NONE) {
        return {};
    }

    effect::color_update upd;
    upd.base.window = &window;

    auto const value = window.readProperty(effi.atom, effi.atom, 32);
    if (value.isNull()) {
        upd.base.valid = false;
        return upd;
    }

    if (value.size() > 0
        && !((value.size() - (16 * sizeof(uint32_t))) % ((4 * sizeof(uint32_t))))) {
        auto cardinals = reinterpret_cast<uint32_t const*>(value.constData());
        auto float_cardinals = reinterpret_cast<float const*>(value.constData());

        unsigned int i = 0;
        for (; i < ((value.size() - (16 * sizeof(uint32_t)))) / sizeof(uint32_t);) {
            int x = cardinals[i++];
            int y = cardinals[i++];
            int w = cardinals[i++];
            int h = cardinals[i++];
            upd.region += QRect(x, y, w, h);
        }

        float color_transform[16];
        for (unsigned int j = 0; j < 16; ++j) {
            color_transform[j] = float_cardinals[i + j];
        }
        upd.color = QMatrix4x4(color_transform);
    }

    return upd;
}

}
