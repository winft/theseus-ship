/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kscreen_update.h"
#include "update.h"

#include "render/x11//effect.h"

#include <kwineffects/effect_integration.h>

#include <map>
#include <string_view>

namespace KWin::render::x11
{

template<typename Effects>
class kscreen_integration : public effect::kscreen_integration
{
public:
    kscreen_integration(Effects& effects)
        : effects{effects}
    {
        QObject::connect(
            &effects, &Effects::propertyNotify, &effects, [this](auto window, long atom) {
                if (!window && atom != XCB_ATOM_NONE && atom == this->atom) {
                    this->update();
                }
            });
        setup_effect_connection_change(*this);
    }

    void add(Effect& effect, update_function const& update) override
    {
        registry.insert({&effect, update});
        atom = announce_support_property(effects, &effect, atom_name.data());
        this->update();
    }

    void remove(Effect& effect) override
    {
        registry.erase(&effect);
        remove_support_property(effects, &effect, atom_name.data());
    }

    void change_state([[maybe_unused]] Effect& effect, double state) override
    {
        assert(registry.find(&effect) != registry.end());
        kscreen_update_state(*this, state);
    }

    void update()
    {
        auto upd = get_kscreen_update(*this);
        if (!upd.base.valid) {
            return;
        }
        for (auto const& [effect, update_call] : registry) {
            update_call(upd);
        }
    }

    std::map<Effect*, update_function> registry;
    Effects& effects;

    long atom{0};
    static constexpr std::string_view atom_name{"_KDE_KWIN_KSCREEN_SUPPORT"};
};

}
