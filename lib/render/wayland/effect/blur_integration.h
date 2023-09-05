/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "blur_update.h"
#include "update.h"

#include "render/x11/effect.h"
#include "render/x11/effect/blur_update.h"

#include <render/effect/interface/effect_integration.h>

#include <Wrapland/Server/blur.h>
#include <Wrapland/Server/display.h>
#include <map>
#include <string_view>
#include <variant>

namespace KWin::render::wayland
{

template<typename Effects>
class blur_integration : public effect::region_integration
{
public:
    blur_integration(Effects& effects, Wrapland::Server::Display& display)
        : effects{effects}
        , internal_properties{get_internal_blur_properties()}
        , display{display}
    {
        setup_effect_window_add(*this);
        setup_effect_internal_window_add(*this);
        setup_effect_screen_geometry_changes(*this);
        setup_effect_xwayland(*this);
    }

    void add(Effect& effect, update_function const& update) override
    {
        registry.insert({&effect, update});

        if (!manager) {
            manager = std::make_unique<Wrapland::Server::BlurManager>(&display);
        }

        // For Xwayland
        atom = x11::announce_support_property(effects, &effect, atom_name.data());

        auto const windows = effects.stackingOrder();
        for (auto window : windows) {
            this->update(*window);
        }
    }

    void remove(Effect& effect) override
    {
        registry.erase(&effect);
        x11::remove_support_property(effects, &effect, atom_name.data());

        if (registry.empty()) {
            manager.reset();
        }
    }

    void reset()
    {
        auto const reg_cp = registry;
        for (auto const& [effect, update_call] : reg_cp) {
            update_call({});
        }
    }

    void update(EffectWindow& window)
    {
        auto upd = get_blur_update(*this, window);
        if (!upd.base.window) {
            // Try Xwayland
            upd = render::x11::get_blur_update(*this, window);
        }
        if (!upd.base.window) {
            return;
        }

        for (auto const& [effect, update_call] : registry) {
            update_call(upd);
        }
    }

    std::map<Effect*, update_function> registry;
    Effects& effects;

    // Surface blur change notifiers
    std::map<EffectWindow*, QMetaObject::Connection> change_notifiers;
    static constexpr Wrapland::Server::surface_change change_ident{
        Wrapland::Server::surface_change::blur};

    decltype(get_internal_blur_properties()) const internal_properties;

    // For Xwayland
    long atom{0};
    static constexpr std::string_view atom_name{"_KDE_NET_WM_BLUR_BEHIND_REGION"};

private:
    std::unique_ptr<Wrapland::Server::BlurManager> manager;
    Wrapland::Server::Display& display;
};

}
