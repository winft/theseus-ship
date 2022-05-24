/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "pointer.h"

#include "config.h"

#include <algorithm>
#include <variant>

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

using cfg_bool = config_data<pointer, bool>;
using cfg_uint = config_data<pointer, quint32>;
using cfg_string = config_data<pointer, QString>;
using cfg_double = config_data<pointer, qreal>;
using cfg_variant = std::variant<cfg_bool, cfg_uint, cfg_string, cfg_double>;

void set_acceleration_from_string(pointer* device, QString const& acceleration)
{
    device->set_acceleration(acceleration.toDouble());
}

QString default_acceleration_to_string(pointer* device)
{
    return QString::number(device->default_acceleration(), 'f', 3);
}

void set_acceleration_pofile_from_int(pointer* device, int code)
{
    auto profile = accel_profile::none;
    if (code == static_cast<int>(accel_profile::adaptive)) {
        profile = accel_profile::adaptive;
    } else if (code == static_cast<int>(accel_profile::flat)) {
        profile = accel_profile::flat;
    }
    device->set_acceleration_profile(profile);
}

int default_acceleration_to_int(pointer* device)
{
    return static_cast<int>(device->default_acceleration_profile());
}

void set_scroll_method_from_int(pointer* device, int code)
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

int default_scroll_method_to_int(pointer* device)
{
    return static_cast<int>(device->default_scroll_method());
}

void set_click_method_from_int(pointer* device, int code)
{
    auto method = clicks::none;
    if (code == static_cast<int>(clicks::button_areas)) {
        method = clicks::button_areas;
    } else if (code == static_cast<int>(clicks::finger_count)) {
        method = clicks::finger_count;
    }
    device->set_click_method(method);
}

int default_click_method_to_int(pointer* device)
{
    return static_cast<int>(device->default_click_method());
}

class pointer_config : public device_config
{
public:
    std::unordered_map<pointer_config_key, cfg_variant> map{
        {
            pointer_config_key::left_handed,
            cfg_bool("LeftHanded",
                     &pointer::set_left_handed,
                     &pointer::left_handed_enabled_by_default),
        },
        {
            pointer_config_key::disable_while_typing,
            cfg_bool("DisableWhileTyping",
                     &pointer::set_disable_while_typing,
                     &pointer::disable_while_typing_enabled_by_default),
        },

        {
            pointer_config_key::acceleration,
            cfg_string("PointerAcceleration",
                       set_acceleration_from_string,
                       default_acceleration_to_string),
        },
        {
            pointer_config_key::acceleration_profile,
            cfg_uint("PointerAccelerationProfile",
                     set_acceleration_pofile_from_int,
                     default_acceleration_to_int),
        },
        {
            pointer_config_key::scroll_method,
            cfg_uint("ScrollMethod", set_scroll_method_from_int, default_scroll_method_to_int),
        },
        {
            pointer_config_key::click_method,
            cfg_uint("ClickMethod", set_click_method_from_int, default_click_method_to_int),
        },

        {
            pointer_config_key::tap_to_click,
            cfg_bool("TapToClick",
                     &pointer::set_tap_to_click,
                     &pointer::tap_to_click_enabled_by_default),
        },
        {
            pointer_config_key::tap_and_drag,
            cfg_bool("TapAndDrag",
                     &pointer::set_tap_and_drag,
                     &pointer::tap_and_drag_enabled_by_default),
        },
        {
            pointer_config_key::tap_drag_lock,
            cfg_bool("TapDragLock",
                     &pointer::set_tap_drag_lock,
                     &pointer::tap_drag_lock_enabled_by_default),
        },
        {
            pointer_config_key::middle_button_emulation,
            cfg_bool("MiddleButtonEmulation",
                     &pointer::set_middle_emulation,
                     &pointer::middle_emulation_enabled_by_default),
        },
        {
            pointer_config_key::lmr_tap_button_map,
            cfg_bool("LmrTapButtonMap",
                     &pointer::set_lmr_tap_button_map,
                     &pointer::lmr_tap_button_map_enabled_by_default),
        },
        {
            pointer_config_key::natural_scroll,
            cfg_bool("NaturalScroll",
                     &pointer::set_natural_scroll,
                     &pointer::natural_scroll_enabled_by_default),
        },

        {
            pointer_config_key::scroll_button,
            cfg_uint("ScrollButton", &pointer::set_scroll_button, &pointer::default_scroll_button),
        },
        {
            pointer_config_key::scroll_factor,
            cfg_double("ScrollFactor",
                       &pointer::set_scroll_factor,
                       &pointer::default_scroll_factor),
        },
    };
};

pointer::pointer(platform* plat)
    : device(new pointer_config, plat)
{
    config = static_cast<pointer_config*>(device::config.get());
}

void pointer::init_config()
{
    device::init_config();
    load_config(this);
}

void pointer::set_tap_to_click(bool active)
{
    if (tap_finger_count() < 1 || is_tap_to_click() == active) {
        return;
    }
    if (!set_tap_to_click_impl(active)) {
        return;
    }
    write_entry(this, pointer_config_key::tap_to_click, active);
    Q_EMIT tap_to_click_changed();
}

void pointer::set_tap_and_drag(bool active)
{
    if (is_tap_and_drag() == active) {
        return;
    }
    if (!set_tap_and_drag_impl(active)) {
        return;
    }
    write_entry(this, pointer_config_key::tap_and_drag, active);
    Q_EMIT tap_and_drag_changed();
}

void pointer::set_tap_drag_lock(bool active)
{
    if (is_tap_drag_lock() == active) {
        return;
    }
    if (!set_tap_drag_lock_impl(active)) {
        return;
    }
    write_entry(this, pointer_config_key::tap_drag_lock, active);
    Q_EMIT tap_drag_lock_changed();
}

void pointer::set_scroll_method(scroll method)
{
    if (!supports_scroll_method(method) || scroll_method() == method) {
        return;
    }
    if (!set_scroll_method_impl(method)) {
        return;
    }
    write_entry(this, pointer_config_key::scroll_method, static_cast<quint32>(method));
    Q_EMIT scroll_method_changed();
}

void pointer::set_lmr_tap_button_map(bool active)
{
    if (!supports_lmr_tap_button_map() || lmr_tap_button_map() == active) {
        return;
    }
    if (!set_lmr_tap_button_map_impl(active)) {
        return;
    }
    write_entry(this, pointer_config_key::lmr_tap_button_map, active);
    Q_EMIT tap_button_map_changed();
}

void pointer::set_middle_emulation(bool active)
{
    if (!supports_middle_emulation() || is_middle_emulation() == active) {
        return;
    }
    if (!set_lmr_tap_button_map_impl(active)) {
        return;
    }
    write_entry(this, pointer_config_key::middle_button_emulation, active);
    Q_EMIT tap_button_map_changed();
}

void pointer::set_natural_scroll(bool active)
{
    if (!supports_natural_scroll() || is_natural_scroll() == active) {
        return;
    }
    if (!set_natural_scroll_impl(active)) {
        return;
    }
    write_entry(this, pointer_config_key::natural_scroll, active);
    Q_EMIT natural_scroll_changed();
}

void pointer::set_scroll_button(uint32_t button)
{
    if (!supports_scroll_method(scroll::on_button_down) || scroll_button() == button) {
        return;
    }
    if (!set_scroll_button_impl(button)) {
        return;
    }
    write_entry(this, pointer_config_key::scroll_button, button);
    Q_EMIT natural_scroll_changed();
}

double pointer::default_scroll_factor() const
{
    return 1.;
}

double pointer::get_scroll_factor() const
{
    return scroll_factor;
}

void pointer::set_scroll_factor(double factor)
{
    if (scroll_factor == factor) {
        return;
    }
    scroll_factor = factor;
    write_entry(this, pointer_config_key::scroll_factor, scroll_factor);
    Q_EMIT scroll_factor_changed();
}

void pointer::set_disable_while_typing(bool active)
{
    if (!supports_disable_while_typing() || is_disable_while_typing() == active) {
        return;
    }
    if (!set_disable_while_typing_impl(active)) {
        return;
    }
    write_entry(this, pointer_config_key::disable_while_typing, active);
    Q_EMIT disable_while_typing_changed();
}

void pointer::set_left_handed(bool active)
{
    if (!supports_left_handed() || is_left_handed() == active) {
        return;
    }
    if (!set_left_handed_impl(active)) {
        return;
    }
    write_entry(this, pointer_config_key::left_handed, active);
    Q_EMIT left_handed_changed();
}

void pointer::set_acceleration(double acceleration)
{
    if (!supports_acceleration()) {
        return;
    }
    acceleration = std::clamp(-1.0, acceleration, 1.0);
    if (!set_acceleration_impl(acceleration)) {
        return;
    }
    write_entry(this, pointer_config_key::acceleration, QString::number(acceleration, 'f', 3));
    Q_EMIT acceleration_changed();
}

void pointer::set_acceleration_profile(accel_profile profile)
{
    if (!supports_acceleration_profile(profile)) {
        return;
    }
    if (!set_acceleration_profile_impl(profile)) {
        return;
    }
    write_entry(this, pointer_config_key::acceleration_profile, static_cast<quint32>(profile));
    Q_EMIT acceleration_profile_changed();
}

void pointer::set_click_method(clicks method)
{
    if (!supports_click_method(method)) {
        return;
    }
    if (!set_click_method_impl(method)) {
        return;
    }
    write_entry(this, pointer_config_key::click_method, static_cast<quint32>(method));
    Q_EMIT click_method_changed();
}

}
