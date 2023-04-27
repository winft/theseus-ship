/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QObject>

namespace KWin::render
{

template<typename EffectIntegrator>
void setup_effect_screen_geometry_changes(EffectIntegrator& effi)
{
    using Effects = typename std::remove_reference<decltype(effi.effects)>::type;

    QObject::connect(&effi.effects, &Effects::screenGeometryChanged, &effi.effects, [&] {
        effi.reset();
        auto const& stacking_order = effi.effects.stackingOrder();
        for (auto const& window : qAsConst(stacking_order)) {
            effi.update(*window);
        }
    });
}

}
