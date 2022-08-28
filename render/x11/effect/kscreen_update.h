/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/logging.h"

#include <kwineffects/effect_integration.h>

#include <xcb/xcb.h>

namespace KWin::render::x11
{

template<typename EffectIntegrator>
effect::fade_update get_kscreen_update(EffectIntegrator& effi)
{
    if (effi.atom == XCB_ATOM_NONE) {
        return {{nullptr, false}, 0};
    }

    auto const value = effi.effects.readRootProperty(effi.atom, XCB_ATOM_CARDINAL, 32);
    if (value.isEmpty()) {
        // Property was deleted. Screen should be faded in.
        return {{nullptr, true}, 1};
    }

    auto data = reinterpret_cast<const uint32_t*>(value.data());
    auto state = 1.;

    switch (*data) {
    case 0:
        // Faded in
        state = 1.;
        break;
    case 1:
        // Fading out
        state = -0.5;
        break;
    case 2:
        // Faded out
        state = -1.;
        break;
    case 3:
        // Fading in
        state = 0.5;
        break;
    default:
        qCDebug(KWIN_CORE)
            << "Incorrect KScreen effect integration Property state, immediate stop: " << data;
        return {{nullptr, true}, 1};
    }

    return {{nullptr, true}, state};
}

template<typename EffectIntegrator>
void kscreen_update_state(EffectIntegrator& effi, double state)
{
    if (effi.atom == XCB_ATOM_NONE) {
        return;
    }

    auto value = -1l;
    if (state == -1) {
        value = 2l;
    } else if (state == 1) {
        value = 0l;
    }

    if (value == -1l) {
        // Not a valid state. Effects may either indicate faded in or out.
        return;
    }

    xcb_change_property(effi.effects.xcbConnection(),
                        XCB_PROP_MODE_REPLACE,
                        effi.effects.x11RootWindow(),
                        effi.atom,
                        XCB_ATOM_CARDINAL,
                        32,
                        1,
                        &value);
}

}
