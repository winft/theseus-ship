/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "pointer.h"

#include "pointer_config.h"

#include <algorithm>
#include <variant>

namespace KWin::input::control
{

pointer::pointer(platform* plat)
    : device(new pointer_config<pointer>, plat)
{
    config = static_cast<pointer_config<pointer>*>(device::config.get());
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
