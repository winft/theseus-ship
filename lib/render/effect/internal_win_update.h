/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "contrast_update.h"
#include "internal_win_properties.h"

#include "utils/algorithm.h"

#include <kwineffects/effect_integration.h>
#include <kwineffects/effect_window.h>

#include <QWindow>

namespace KWin::render
{

template<typename EffectIntegrator>
void setup_effect_internal_window_add(EffectIntegrator& effi)
{
    using Effects = typename std::remove_reference<decltype(effi.effects)>::type;

    QObject::connect(&effi.effects, &Effects::windowAdded, &effi.effects, [&](auto window) {
        if (auto internal = window->internalWindow()) {
            internal->installEventFilter(&effi.effects);
            effi.update(*window);
        }
    });
}

template<typename EffectIntegrator>
bool handle_internal_window_effect_update_event(EffectIntegrator& effi,
                                                QObject* watched,
                                                QEvent* event)
{
    auto internal = qobject_cast<QWindow*>(watched);

    if (!internal || event->type() != QEvent::DynamicPropertyChange) {
        return false;
    }

    auto const& pe_name = static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName();
    if (!contains_if(effi.internal_properties, [&](auto const& prop_var) {
            auto match{false};
            std::visit([&](auto&& prop) { match = pe_name == prop.name.data(); }, prop_var);
            return match;
        })) {
        return false;
    }

    if (auto window = effi.effects.findWindow(internal)) {
        effi.update(*window);
    }
    return false;
}

template<typename Prop>
auto get_internal_window_prop_val(Prop const& prop, QWindow& window, bool& ok) ->
    typename Prop::type
{
    auto const& qt_var = window.property(prop.name.data());
    auto val = prop.convert(qt_var, ok);
    if (!ok) {
        return {};
    }
    return val;
}

template<typename Prop>
auto get_internal_window_prop_val_or_default(Prop const& prop,
                                             QWindow& window,
                                             typename Prop::type default_val) -> typename Prop::type
{
    bool ok;
    auto const& qt_var = window.property(prop.name.data());
    auto val = prop.convert(qt_var, ok);
    if (!ok) {
        return default_val;
    }
    return val;
}

inline effect::region_update get_internal_window_region_update(internal_region_property const& prop,
                                                               EffectWindow& window)
{
    auto internal = window.internalWindow();
    assert(internal);

    bool ok;
    auto val = get_internal_window_prop_val(prop, *internal, ok);
    if (!ok) {
        return {};
    }
    return {{&window, true}, val};
}

template<typename EffectIntegrator>
effect::region_update get_internal_window_blur_update(EffectIntegrator& effi, EffectWindow& window)
{
    auto internal = window.internalWindow();
    if (!internal) {
        return {};
    }

    return get_internal_window_region_update(
        std::get<internal_region_property>(effi.internal_properties.front()), window);
}

template<typename EffectIntegrator>
effect::color_update get_internal_window_contrast_update(EffectIntegrator& effi,
                                                         EffectWindow& window)
{
    auto internal = window.internalWindow();
    if (!internal) {
        return {};
    }

    auto region_upd = get_internal_window_region_update(
        std::get<internal_region_property>(effi.internal_properties.at(0)), window);

    if (!region_upd.base.valid) {
        return {};
    }

    auto contrast = get_internal_window_prop_val_or_default(
        std::get<internal_double_property>(effi.internal_properties.at(1)), *internal, 1.);
    auto intensity = get_internal_window_prop_val_or_default(
        std::get<internal_double_property>(effi.internal_properties.at(2)), *internal, 1.);
    auto saturation = get_internal_window_prop_val_or_default(
        std::get<internal_double_property>(effi.internal_properties.at(3)), *internal, 1.);

    return {region_upd.base,
            region_upd.value,
            get_contrast_color_matrix(contrast, intensity, saturation)};
}

template<typename EffectIntegrator>
effect::anim_update get_internal_window_slide_update(EffectIntegrator& effi, EffectWindow& window)
{
    auto internal = window.internalWindow();
    if (!internal) {
        return {};
    }

    auto location = get_internal_window_prop_val_or_default(
        std::get<internal_slide_from_location_property>(effi.internal_properties.at(0)),
        *internal,
        slide_from_location_edge::none);

    auto get_slide_position = [](auto loc) {
        switch (loc) {
        case slide_from_location_edge::bottom:
            return effect::position::bottom;
        case slide_from_location_edge::top:
            return effect::position::top;
        case slide_from_location_edge::right:
            return effect::position::right;
        case slide_from_location_edge::left:
            return effect::position::left;
        default:
            return effect::position::center;
        }
    };
    auto pos = get_slide_position(location);
    if (pos == effect::position::center) {
        return {};
    }

    auto offset = get_internal_window_prop_val_or_default(
        std::get<internal_int_property>(effi.internal_properties.at(1)), *internal, -1);

    return {{&window, true}, pos, {}, {}, static_cast<double>(offset), 0};
}

}
