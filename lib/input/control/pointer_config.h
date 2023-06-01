/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "config.h"
#include "pointer_types.h"

namespace KWin::input::control
{

enum class pointer_config_key {
    left_handed,
    disable_while_typing,
    acceleration,
    acceleration_profile,
    tap_to_click,
    lmr_tap_button_map,
    tap_and_drag,
    tap_drag_lock,
    middle_button_emulation,
    natural_scroll,
    scroll_method,
    scroll_button,
    click_method,
    scroll_factor,
};

template<typename Pointer>
void set_acceleration_from_string(Pointer* device, QString const& acceleration)
{
    device->set_acceleration(acceleration.toDouble());
}

template<typename Pointer>
QString default_acceleration_to_string(Pointer* device)
{
    return QString::number(device->default_acceleration(), 'f', 3);
}

template<typename Pointer>
void set_acceleration_pofile_from_int(Pointer* device, int code)
{
    auto profile = accel_profile::none;
    if (code == static_cast<int>(accel_profile::adaptive)) {
        profile = accel_profile::adaptive;
    } else if (code == static_cast<int>(accel_profile::flat)) {
        profile = accel_profile::flat;
    }
    device->set_acceleration_profile(profile);
}

template<typename Pointer>
int default_acceleration_to_int(Pointer* device)
{
    return static_cast<int>(device->default_acceleration_profile());
}

template<typename Pointer>
void set_scroll_method_from_int(Pointer* device, int code)
{
    auto method = scroll::none;
    if (code == static_cast<int>(scroll::edge)) {
        method = scroll::edge;
    } else if (code == static_cast<int>(scroll::on_button_down)) {
        method = scroll::on_button_down;
    } else if (code == static_cast<int>(scroll::two_finger)) {
        method = scroll::two_finger;
    }
    device->set_scroll_method(method);
}

template<typename Pointer>
int default_scroll_method_to_int(Pointer* device)
{
    return static_cast<int>(device->default_scroll_method());
}

template<typename Pointer>
void set_click_method_from_int(Pointer* device, int code)
{
    auto method = clicks::none;
    if (code == static_cast<int>(clicks::button_areas)) {
        method = clicks::button_areas;
    } else if (code == static_cast<int>(clicks::finger_count)) {
        method = clicks::finger_count;
    }
    device->set_click_method(method);
}

template<typename Pointer>
int default_click_method_to_int(Pointer* device)
{
    return static_cast<int>(device->default_click_method());
}

template<typename Pointer>
class pointer_config : public device_config
{
public:
    using cfg_bool = config_data<Pointer, bool>;
    using cfg_uint = config_data<Pointer, quint32>;
    using cfg_string = config_data<Pointer, QString>;
    using cfg_double = config_data<Pointer, qreal>;
    using cfg_variant = std::variant<cfg_bool, cfg_uint, cfg_string, cfg_double>;

    std::unordered_map<pointer_config_key, cfg_variant> map{
        {
            pointer_config_key::left_handed,
            cfg_bool("LeftHanded",
                     &Pointer::set_left_handed,
                     &Pointer::left_handed_enabled_by_default),
        },
        {
            pointer_config_key::disable_while_typing,
            cfg_bool("DisableWhileTyping",
                     &Pointer::set_disable_while_typing,
                     &Pointer::disable_while_typing_enabled_by_default),
        },

        {
            pointer_config_key::acceleration,
            cfg_string("PointerAcceleration",
                       set_acceleration_from_string<Pointer>,
                       default_acceleration_to_string<Pointer>),
        },
        {
            pointer_config_key::acceleration_profile,
            cfg_uint("PointerAccelerationProfile",
                     set_acceleration_pofile_from_int<Pointer>,
                     default_acceleration_to_int<Pointer>),
        },
        {
            pointer_config_key::scroll_method,
            cfg_uint("ScrollMethod",
                     set_scroll_method_from_int<Pointer>,
                     default_scroll_method_to_int<Pointer>),
        },
        {
            pointer_config_key::click_method,
            cfg_uint("ClickMethod",
                     set_click_method_from_int<Pointer>,
                     default_click_method_to_int<Pointer>),
        },

        {
            pointer_config_key::tap_to_click,
            cfg_bool("TapToClick",
                     &Pointer::set_tap_to_click,
                     &Pointer::tap_to_click_enabled_by_default),
        },
        {
            pointer_config_key::tap_and_drag,
            cfg_bool("TapAndDrag",
                     &Pointer::set_tap_and_drag,
                     &Pointer::tap_and_drag_enabled_by_default),
        },
        {
            pointer_config_key::tap_drag_lock,
            cfg_bool("TapDragLock",
                     &Pointer::set_tap_drag_lock,
                     &Pointer::tap_drag_lock_enabled_by_default),
        },
        {
            pointer_config_key::middle_button_emulation,
            cfg_bool("MiddleButtonEmulation",
                     &Pointer::set_middle_emulation,
                     &Pointer::middle_emulation_enabled_by_default),
        },
        {
            pointer_config_key::lmr_tap_button_map,
            cfg_bool("LmrTapButtonMap",
                     &Pointer::set_lmr_tap_button_map,
                     &Pointer::lmr_tap_button_map_enabled_by_default),
        },
        {
            pointer_config_key::natural_scroll,
            cfg_bool("NaturalScroll",
                     &Pointer::set_natural_scroll,
                     &Pointer::natural_scroll_enabled_by_default),
        },

        {
            pointer_config_key::scroll_button,
            cfg_uint("ScrollButton", &Pointer::set_scroll_button, &Pointer::default_scroll_button),
        },
        {
            pointer_config_key::scroll_factor,
            cfg_double("ScrollFactor",
                       &Pointer::set_scroll_factor,
                       &Pointer::default_scroll_factor),
        },
    };
};

}
