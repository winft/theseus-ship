/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/effect/contrast_update.h"
#include "render/effect/internal_win_update.h"

#include <render/effect/interface/effect_integration.h>
#include <render/effect/interface/effect_window.h>

#include <Wrapland/Server/contrast.h>
#include <Wrapland/Server/surface.h>

namespace KWin::render::wayland
{

template<typename EffectIntegrator>
effect::color_update get_contrast_update(EffectIntegrator& effi, EffectWindow& window)
{
    if (auto internal_upd = get_internal_window_contrast_update(effi, window);
        internal_upd.base.window) {
        return internal_upd;
    }

    auto surface = window.surface();
    if (!surface) {
        return {};
    }

    auto& contrast = surface->state().contrast;
    if (!contrast) {
        return {};
    }

    return {{&window, true},
            contrast->region(),
            get_contrast_color_matrix(
                contrast->contrast(), contrast->intensity(), contrast->saturation())};
}

}
