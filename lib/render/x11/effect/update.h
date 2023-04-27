/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/effect/integration.h"
#include "render/effect/internal_win_update.h"

#include <xcb/xcb.h>

namespace KWin::render::x11
{

template<typename EffectIntegrator>
void setup_effect_connection_change(EffectIntegrator& effi)
{
    using Effects = typename std::remove_reference<decltype(effi.effects)>::type;

    QObject::connect(&effi.effects, &Effects::xcbConnectionChanged, &effi.effects, [&] {
        if (!effi.registry.empty()) {
            effi.atom = effi.effects.announceSupportProperty(effi.atom_name.data(),
                                                             effi.registry.begin()->first);
        }
    });
}

template<typename EffectIntegrator>
void setup_effect_property_notify(EffectIntegrator& effi)
{
    using Effects = typename std::remove_reference<decltype(effi.effects)>::type;

    QObject::connect(
        &effi.effects, &Effects::propertyNotify, &effi.effects, [&](auto window, long atom) {
            if (window && atom != XCB_ATOM_NONE && atom == effi.atom) {
                effi.update(*window);
            }
        });
}

template<typename EffectIntegrator>
void setup_effect_window_add(EffectIntegrator& effi)
{
    using Effects = typename std::remove_reference<decltype(effi.effects)>::type;

    QObject::connect(&effi.effects, &Effects::windowAdded, &effi.effects, [&](auto window) {
        effi.update(*window);
    });
}

}
