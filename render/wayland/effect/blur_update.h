/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwineffects/effect_integration.h>
#include <kwineffects/effect_window.h>

#include <Wrapland/Server/blur.h>
#include <Wrapland/Server/surface.h>

namespace KWin::render::wayland
{

template<typename EffectIntegrator>
effect::region_update get_blur_update(EffectIntegrator& effi, EffectWindow& window)
{
    if (auto internal_upd = get_internal_window_blur_update(effi, window);
        internal_upd.base.window) {
        return internal_upd;
    }

    auto surface = window.surface();
    if (!surface || !surface->state().blur) {
        return {};
    }

    return {&window, true, surface->state().blur->region()};
}

}
