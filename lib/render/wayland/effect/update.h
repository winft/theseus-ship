/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/effect/integration.h"
#include "render/effect/internal_win_update.h"
#include "render/x11/effect/update.h"

#include <Wrapland/Server/surface.h>

namespace KWin::render::wayland
{

template<typename EffectIntegrator>
void setup_effect_window_remove(EffectIntegrator& effi)
{
    using Effects = typename std::remove_reference<decltype(effi.effects)>::type;

    QObject::connect(&effi.effects, &Effects::windowDeleted, &effi.effects, [&](auto window) {
        auto& notifiers = effi.change_notifiers;
        auto it = notifiers.find(window);
        if (it != notifiers.end()) {
            QObject::disconnect(it->second);
            notifiers.erase(it);
        }
    });
}

template<typename EffectIntegrator>
void setup_effect_window_add(EffectIntegrator& effi)
{
    using Effects = typename std::remove_reference<decltype(effi.effects)>::type;

    QObject::connect(&effi.effects, &Effects::windowAdded, &effi.effects, [&](auto window) {
        if (auto surface = window->surface()) {
            effi.change_notifiers[window]
                = QObject::connect(surface,
                                   &Wrapland::Server::Surface::committed,
                                   &effi.effects,
                                   [&, surface, window] {
                                       if (window && surface->state().updates & effi.change_ident) {
                                           effi.update(*window);
                                       }
                                   });
        }

        effi.update(*window);
    });

    // Also need to clean up again on remove.
    setup_effect_window_remove(effi);
}

template<typename EffectIntegrator>
void setup_effect_xwayland(EffectIntegrator& effi)
{
    x11::setup_effect_property_notify(effi);
    x11::setup_effect_connection_change(effi);
}

}
