/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "slide_update.h"
#include "update.h"

#include "render/x11//effect.h"

#include <kwineffects/effect_integration.h>

#include <map>
#include <string_view>
#include <variant>

namespace KWin::render::x11
{

template<typename Effects>
class slide_integration : public effect::anim_integration
{
public:
    slide_integration(Effects& effects)
        : effects{effects}
        , internal_properties{get_internal_slide_properties()}
    {
        setup_effect_window_add(*this);
        setup_effect_property_notify(*this);
        setup_effect_internal_window_add(*this);
    }

    void add(Effect& effect, update_function const& update) override
    {
        registry.insert({&effect, update});
        atom = announce_support_property(effects, &effect, atom_name.data());

        auto const windows = effects.stackingOrder();
        for (auto window : windows) {
            this->update(*window);
        }
    }

    void remove(Effect& effect) override
    {
        registry.erase(&effect);
        remove_support_property(effects, &effect, atom_name.data());
    }

    void update(EffectWindow& window)
    {
        auto upd = get_slide_update(*this, window);
        if (!upd.base.window) {
            return;
        }

        auto const reg_cp = registry;
        for (auto const& [effect, update_call] : reg_cp) {
            update_call(upd);
        }
    }

    std::map<Effect*, update_function> registry;
    Effects& effects;

    long atom{0};
    static constexpr std::string_view atom_name{"_KDE_SLIDE"};
    decltype(get_internal_slide_properties()) const internal_properties;
};

}
