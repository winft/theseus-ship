/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/x11/effect/contrast_update.h"
#include <render/wayland/effect/blur_integration.h>
#include <render/wayland/effect/contrast_integration.h>
#include <render/wayland/effect/slide_integration.h>
#include <render/x11/effect/blur_update.h>
#include <render/x11/effect/slide_update.h>
#include <render/x11/effect/update.h>

namespace KWin::render::wayland
{

template<typename EffectIntegrator>
void setup_effect_xwayland(EffectIntegrator& effi)
{
    x11::setup_effect_property_notify(effi);
    x11::setup_effect_connection_change(effi);
}

struct xwl_blur_support {
    template<typename EffectIntegrator>
    void setup(EffectIntegrator& effi)
    {
        base.setup(effi);
        setup_effect_xwayland(effi);
    }

    template<typename EffectIntegrator>
    void add(EffectIntegrator& effi,
             Effect& effect,
             typename EffectIntegrator::update_function const& update)
    {
        effi.registry.insert({&effect, update});

        if (!effi.manager) {
            effi.manager = std::make_unique<Wrapland::Server::BlurManager>(&effi.display);
        }

        atom = x11::announce_support_property(effi.effects, &effect, atom_name.data());

        auto const windows = effi.effects.stackingOrder();
        for (auto window : windows) {
            effi.update(*window);
        }
    }

    template<typename EffectIntegrator>
    void remove(EffectIntegrator& effi, Effect& effect)
    {
        effi.registry.erase(&effect);
        x11::remove_support_property(effi.effects, &effect, atom_name.data());

        if (effi.registry.empty()) {
            effi.manager.reset();
        }
    }

    template<typename EffectIntegrator>
    void update(EffectIntegrator& effi, EffectWindow& window)
    {
        auto upd = get_blur_update(effi, window);
        if (!upd.base.window) {
            // Try Xwayland
            upd = render::x11::get_blur_update(effi, window);
        }
        if (!upd.base.window) {
            return;
        }

        for (auto const& [effect, update_call] : effi.registry) {
            update_call(upd);
        }
    }

    blur_support base;
    long atom{0};
    static constexpr std::string_view atom_name{"_KDE_NET_WM_BLUR_BEHIND_REGION"};
};

struct xwl_contrast_support {
    template<typename EffectIntegrator>
    void setup(EffectIntegrator& effi)
    {
        base.setup(effi);
        setup_effect_xwayland(effi);
    }

    template<typename EffectIntegrator>
    void add(EffectIntegrator& effi,
             Effect& effect,
             typename EffectIntegrator::update_function const& update)
    {
        effi.registry.insert({&effect, update});

        if (!effi.manager) {
            effi.manager = std::make_unique<Wrapland::Server::ContrastManager>(&effi.display);
        }

        atom = x11::announce_support_property(effi.effects, &effect, atom_name.data());

        auto const windows = effi.effects.stackingOrder();
        for (auto window : windows) {
            effi.update(*window);
        }
    }

    template<typename EffectIntegrator>
    void remove(EffectIntegrator& effi, Effect& effect)
    {
        effi.registry.erase(&effect);
        x11::remove_support_property(effi.effects, &effect, atom_name.data());

        if (effi.registry.empty()) {
            effi.manager.reset();
        }
    }

    template<typename EffectIntegrator>
    void update(EffectIntegrator& effi, EffectWindow& window)
    {
        auto upd = get_contrast_update(effi, window);
        if (!upd.base.window) {
            // Try Xwayland
            upd = render::x11::get_contrast_update(effi, window);
        }
        if (!upd.base.window) {
            return;
        }

        for (auto const& [effect, update_call] : effi.registry) {
            update_call(upd);
        }
    }

    contrast_support base;
    long atom{0};
    static constexpr std::string_view atom_name{"_KDE_NET_WM_BACKGROUND_CONTRAST_REGION"};
};

struct xwl_slide_support {
    template<typename EffectIntegrator>
    void setup(EffectIntegrator& effi)
    {
        base.setup(effi);
        setup_effect_xwayland(effi);
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

        atom = x11::announce_support_property(effi.effects, &effect, atom_name.data());

        auto const windows = effi.effects.stackingOrder();
        for (auto window : windows) {
            effi.update(*window);
        }
    }

    template<typename EffectIntegrator>
    void remove(EffectIntegrator& effi, Effect& effect)
    {
        effi.registry.erase(&effect);
        x11::remove_support_property(effi.effects, &effect, atom_name.data());

        if (effi.registry.empty()) {
            effi.manager.reset();
        }
    }

    template<typename EffectIntegrator>
    void update(EffectIntegrator& effi, EffectWindow& window)
    {
        auto upd = get_slide_update(effi, window);
        if (!upd.base.window) {
            // Try Xwayland
            upd = render::x11::get_slide_update(effi, window);
        }
        if (!upd.base.window) {
            return;
        }

        for (auto const& [effect, update_call] : effi.registry) {
            update_call(upd);
        }
    }

    slide_support base;
    long atom{0};
    static constexpr std::string_view atom_name{"_KDE_SLIDE"};
};

}
