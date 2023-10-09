/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "slide_update.h"
#include "update.h"

#include "render/x11/effect.h"
#include "render/x11/effect/slide_update.h"

#include <render/effect/interface/effect_integration.h>

#include <Wrapland/Server/display.h>
#include <Wrapland/Server/slide.h>
#include <map>
#include <string_view>
#include <variant>

namespace KWin::render::wayland
{

struct slide_support {
    template<typename EffectIntegrator>
    void setup(EffectIntegrator& effi)
    {
        setup_effect_window_add(effi);
        setup_effect_internal_window_add(effi);
    }

    template<typename EffectIntegrator>
    void add(EffectIntegrator& effi,
             Effect& effect,
             typename EffectIntegrator::update_function const& update)
    {
        effi.registry.insert({&effect, update});

        if (!effi.manager) {
            effi.manager = std::make_unique<Wrapland::Server::SlideManager>(&effi.display);
        }

        auto const windows = effi.effects.stackingOrder();
        for (auto window : windows) {
            effi.update(*window);
        }
    }

    template<typename EffectIntegrator>
    void remove(EffectIntegrator& effi, Effect& effect)
    {
        effi.registry.erase(&effect);
        if (effi.registry.empty()) {
            effi.manager.reset();
        }
    }

    template<typename EffectIntegrator>
    void update(EffectIntegrator& effi, EffectWindow& window)
    {
        auto upd = get_slide_update(effi, window);
        if (!upd.base.window) {
            return;
        }

        for (auto const& [effect, update_call] : effi.registry) {
            update_call(upd);
        }
    }
};

template<typename Effects, typename Support>
class slide_integration : public effect::anim_integration
{
public:
    slide_integration(Effects& effects, Wrapland::Server::Display& display)
        : effects{effects}
        , internal_properties{get_internal_slide_properties()}
        , display{display}
    {
        support.setup(*this);
    }

    void add(Effect& effect, update_function const& update) override
    {
        support.add(*this, effect, update);
    }

    void remove(Effect& effect) override
    {
        support.remove(*this, effect);
    }

    void update(EffectWindow& window)
    {
        support.update(*this, window);
    }

    Support support;
    std::map<Effect*, update_function> registry;
    Effects& effects;

    // Surface slide change notifiers
    std::map<EffectWindow*, QMetaObject::Connection> change_notifiers;
    static constexpr Wrapland::Server::surface_change change_ident{
        Wrapland::Server::surface_change::slide};
    decltype(get_internal_slide_properties()) const internal_properties;

    std::unique_ptr<Wrapland::Server::SlideManager> manager;
    Wrapland::Server::Display& display;
};

}
