/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/effect/internal_win_update.h"

#include <kwineffects/effect_integration.h>
#include <kwineffects/effect_window.h>

#include <Wrapland/Server/slide.h>
#include <Wrapland/Server/surface.h>

namespace KWin::render::wayland
{

template<typename EffectIntegrator>
effect::anim_update get_slide_update(EffectIntegrator& effi, EffectWindow& window)
{
    if (auto internal_upd = get_internal_window_slide_update(effi, window);
        internal_upd.base.window) {
        return internal_upd;
    }

    auto surface = window.surface();
    if (!surface) {
        return {};
    }

    auto& slide = surface->state().slide;
    if (!slide) {
        return {};
    }

    auto get_slide_position = [](auto loc) {
        switch (loc) {
        case Wrapland::Server::Slide::Location::Bottom:
            return effect::position::bottom;
        case Wrapland::Server::Slide::Location::Top:
            return effect::position::top;
        case Wrapland::Server::Slide::Location::Right:
            return effect::position::right;
        case Wrapland::Server::Slide::Location::Left:
            return effect::position::left;
        default:
            assert(false);
            return effect::position::center;
        }
    };

    // Per convention offset might be -1 to indicate the effect should choose. So first cast to int.
    auto offset_as_int = static_cast<int>(slide->offset());

    return {{&window, true},
            get_slide_position(slide->location()),
            {},
            {},
            static_cast<double>(offset_as_int),
            0};
}

}
