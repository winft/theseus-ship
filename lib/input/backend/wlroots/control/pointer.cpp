/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "pointer.h"

#include "control.h"

#include <linux/input.h>

namespace KWin::input::backend::wlroots
{

pointer_control::pointer_control(libinput_device* dev, KSharedConfigPtr input_config)
    : dev{dev}
{
    init_device_control(this, input_config);

    auto query_button = [this](auto code, auto button) {
        if (libinput_device_pointer_has_button(this->dev, code) == 1) {
            buttons |= button;
        }
    };
    query_button(BTN_LEFT, Qt::LeftButton);
    query_button(BTN_MIDDLE, Qt::MiddleButton);
    query_button(BTN_RIGHT, Qt::RightButton);
    query_button(BTN_SIDE, Qt::ExtraButton1);
    query_button(BTN_EXTRA, Qt::ExtraButton2);
    query_button(BTN_BACK, Qt::BackButton);
    query_button(BTN_FORWARD, Qt::ForwardButton);
    query_button(BTN_TASK, Qt::TaskButton);
}

bool pointer_control::is_enabled() const
{
    return is_enabled_backend(this);
}

bool pointer_control::set_enabled_impl(bool enabled)
{
    return set_enabled_backend(this, enabled);
}

bool pointer_control::is_touchpad() const
{
    // Ignore combined devices, for example to not toggle the keyboard off for a touchpad installed
    // on a keyboard.
    auto special_hw = libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_KEYBOARD)
        || libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_TOUCH)
        || libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_TABLET_TOOL);

    // Further increase the chance it's really a touchpad by doing some sanity checks on the device.
    auto sanity_check = tap_finger_count() > 0 && supports_disable_while_typing()
        && supports_disable_events_on_external_mouse();
    return !special_hw && sanity_check;
}

bool pointer_control::supports_gesture() const
{
    return libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_GESTURE);
}

bool pointer_control::supports_disable_events() const
{
    return supports_disable_events_backend(this);
}

QSizeF pointer_control::size() const
{
    return size_backend(this);
}

Qt::MouseButtons pointer_control::supported_buttons() const
{
    return buttons;
}

int pointer_control::tap_finger_count() const
{
    return libinput_device_config_tap_get_finger_count(dev);
}

bool pointer_control::supports_disable_events_on_external_mouse() const
{
    return libinput_device_config_send_events_get_modes(dev)
        & LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE;
}

bool pointer_control::tap_to_click_enabled_by_default()
{
    return libinput_device_config_tap_get_default_enabled(dev) == LIBINPUT_CONFIG_TAP_ENABLED;
}

bool pointer_control::is_tap_to_click() const
{
    return libinput_device_config_tap_get_enabled(dev);
}

bool pointer_control::set_tap_to_click_impl(bool active)
{
    auto val = active ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED;
    return libinput_device_config_tap_set_enabled(dev, val) == LIBINPUT_CONFIG_STATUS_SUCCESS;
}

bool pointer_control::tap_and_drag_enabled_by_default() const
{
    return libinput_device_config_tap_get_default_drag_enabled(dev);
}

bool pointer_control::is_tap_and_drag() const
{
    return libinput_device_config_tap_get_drag_enabled(dev);
}

bool pointer_control::set_tap_and_drag_impl(bool active)
{
    auto val = active ? LIBINPUT_CONFIG_DRAG_ENABLED : LIBINPUT_CONFIG_DRAG_DISABLED;
    return libinput_device_config_tap_set_drag_enabled(dev, val) == LIBINPUT_CONFIG_STATUS_SUCCESS;
}

bool pointer_control::tap_drag_lock_enabled_by_default() const
{
    return libinput_device_config_tap_get_default_drag_lock_enabled(dev);
}

bool pointer_control::is_tap_drag_lock() const
{
    return libinput_device_config_tap_get_drag_lock_enabled(dev);
}

bool pointer_control::set_tap_drag_lock_impl(bool active)
{
    auto val = active ? LIBINPUT_CONFIG_DRAG_LOCK_ENABLED : LIBINPUT_CONFIG_DRAG_LOCK_DISABLED;
    return libinput_device_config_tap_set_drag_lock_enabled(dev, val)
        == LIBINPUT_CONFIG_STATUS_SUCCESS;
}

bool pointer_control::supports_disable_while_typing() const
{
    return libinput_device_config_dwt_is_available(dev);
}

bool pointer_control::disable_while_typing_enabled_by_default() const
{
    return libinput_device_config_dwt_get_default_enabled(dev);
}

bool pointer_control::is_disable_while_typing() const
{
    return libinput_device_config_dwt_get_enabled(dev) == LIBINPUT_CONFIG_DWT_ENABLED;
}

bool pointer_control::set_disable_while_typing_impl(bool active)
{
    auto val = active ? LIBINPUT_CONFIG_DWT_ENABLED : LIBINPUT_CONFIG_DWT_DISABLED;
    return libinput_device_config_dwt_set_enabled(dev, val) == LIBINPUT_CONFIG_STATUS_SUCCESS;
}

bool pointer_control::supports_left_handed() const
{
    return libinput_device_config_left_handed_is_available(dev);
}

bool pointer_control::left_handed_enabled_by_default() const
{
    return libinput_device_config_left_handed_get_default(dev);
}

bool pointer_control::is_left_handed() const
{
    return libinput_device_config_left_handed_get(dev);
}

bool pointer_control::set_left_handed_impl(bool active)
{
    auto ret
        = libinput_device_config_left_handed_set(dev, active) == LIBINPUT_CONFIG_STATUS_SUCCESS;
    return ret;
}

bool pointer_control::supports_middle_emulation() const
{
    return libinput_device_config_middle_emulation_is_available(dev);
}

bool pointer_control::middle_emulation_enabled_by_default() const
{
    return libinput_device_config_middle_emulation_get_default_enabled(dev)
        == LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED;
}

bool pointer_control::is_middle_emulation() const
{
    return libinput_device_config_middle_emulation_get_enabled(dev)
        == LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED;
}

bool pointer_control::set_middle_emulation_impl(bool active)
{
    auto val = active ? LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED
                      : LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED;
    return libinput_device_config_middle_emulation_set_enabled(dev, val)
        == LIBINPUT_CONFIG_STATUS_SUCCESS;
}

bool pointer_control::supports_natural_scroll() const
{
    return libinput_device_config_scroll_has_natural_scroll(dev);
}

bool pointer_control::natural_scroll_enabled_by_default() const
{
    return libinput_device_config_scroll_get_default_natural_scroll_enabled(dev);
}

bool pointer_control::is_natural_scroll() const
{
    return libinput_device_config_scroll_get_natural_scroll_enabled(dev);
}

bool pointer_control::set_natural_scroll_impl(bool active)
{
    return libinput_device_config_scroll_set_natural_scroll_enabled(dev, active)
        == LIBINPUT_CONFIG_STATUS_SUCCESS;
}

auto to_libinput_scroll_method(control::scroll method)
{
    switch (method) {
    case control::scroll::edge:
        return LIBINPUT_CONFIG_SCROLL_EDGE;
    case control::scroll::on_button_down:
        return LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN;
    case control::scroll::two_finger:
        return LIBINPUT_CONFIG_SCROLL_2FG;
    case control::scroll::none:
    default:
        return LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
    };
}

auto from_libinput_scroll_method(libinput_config_scroll_method method)
{
    switch (method) {
    case LIBINPUT_CONFIG_SCROLL_EDGE:
        return control::scroll::edge;
    case LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN:
        return control::scroll::on_button_down;
    case LIBINPUT_CONFIG_SCROLL_2FG:
        return control::scroll::two_finger;
    case LIBINPUT_CONFIG_SCROLL_NO_SCROLL:
    default:
        return control::scroll::none;
    };
}

bool pointer_control::supports_scroll_method(control::scroll method) const
{
    auto methods = libinput_device_config_scroll_get_methods(dev);
    return to_libinput_scroll_method(method) & methods;
}

control::scroll pointer_control::default_scroll_method() const
{
    return from_libinput_scroll_method(libinput_device_config_scroll_get_default_method(dev));
}

control::scroll pointer_control::scroll_method() const
{
    return from_libinput_scroll_method(libinput_device_config_scroll_get_method(dev));
}

bool pointer_control::set_scroll_method_impl(control::scroll method)
{
    auto val = to_libinput_scroll_method(method);
    return libinput_device_config_scroll_set_method(dev, val);
}

bool pointer_control::supports_lmr_tap_button_map() const
{
    return tap_finger_count() > 1;
}

bool pointer_control::lmr_tap_button_map_enabled_by_default() const
{
    return libinput_device_config_tap_get_default_button_map(dev) == LIBINPUT_CONFIG_TAP_MAP_LMR;
}

bool pointer_control::lmr_tap_button_map() const
{
    return libinput_device_config_tap_get_button_map(dev) & LIBINPUT_CONFIG_TAP_MAP_LMR;
}

bool pointer_control::set_lmr_tap_button_map_impl(bool active)
{
    auto val = active ? LIBINPUT_CONFIG_TAP_MAP_LMR : LIBINPUT_CONFIG_TAP_MAP_LRM;
    return libinput_device_config_tap_set_button_map(dev, val) == LIBINPUT_CONFIG_STATUS_SUCCESS;
}

uint32_t pointer_control::default_scroll_button() const
{
    return libinput_device_config_scroll_get_default_button(dev);
}

uint32_t pointer_control::scroll_button() const
{
    return libinput_device_config_scroll_get_button(dev);
}

bool pointer_control::set_scroll_button_impl(uint32_t button)
{
    return libinput_device_config_scroll_set_button(dev, button) == LIBINPUT_CONFIG_STATUS_SUCCESS;
}

bool pointer_control::supports_acceleration() const
{
    return libinput_device_config_accel_is_available(dev);
}

double pointer_control::default_acceleration() const
{
    return libinput_device_config_accel_get_default_speed(dev);
}

double pointer_control::acceleration() const
{
    return libinput_device_config_accel_get_speed(dev);
}

bool pointer_control::set_acceleration_impl(double acceleration)
{
    return libinput_device_config_accel_set_speed(dev, acceleration)
        == LIBINPUT_CONFIG_STATUS_SUCCESS;
}

auto to_libinput_accel_profile(control::accel_profile profile)
{
    switch (profile) {
    case control::accel_profile::adaptive:
        return LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
    case control::accel_profile::flat:
        return LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
    case control::accel_profile::none:
    default:
        return LIBINPUT_CONFIG_ACCEL_PROFILE_NONE;
    };
}

auto from_libinput_accel_profile(libinput_config_accel_profile profile)
{
    switch (profile) {
    case LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE:
        return control::accel_profile::adaptive;
    case LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT:
        return control::accel_profile::flat;
    case LIBINPUT_CONFIG_ACCEL_PROFILE_NONE:
    default:
        return control::accel_profile::none;
    };
}

bool pointer_control::supports_acceleration_profile(control::accel_profile profile) const
{
    auto profiles = libinput_device_config_accel_get_profiles(dev);
    return to_libinput_accel_profile(profile) & profiles;
}

control::accel_profile pointer_control::default_acceleration_profile() const
{
    auto profile = libinput_device_config_accel_get_default_profile(dev);
    return from_libinput_accel_profile(profile);
}

control::accel_profile pointer_control::acceleration_profile() const
{
    auto profile = libinput_device_config_accel_get_profile(dev);
    return from_libinput_accel_profile(profile);
}

bool pointer_control::set_acceleration_profile_impl(control::accel_profile profile)
{
    auto val = to_libinput_accel_profile(profile);
    return libinput_device_config_accel_set_profile(dev, val) == LIBINPUT_CONFIG_STATUS_SUCCESS;
}

auto to_libinput_click_method(control::clicks method)
{
    switch (method) {
    case control::clicks::button_areas:
        return LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
    case control::clicks::finger_count:
        return LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER;
    case control::clicks::none:
    default:
        return LIBINPUT_CONFIG_CLICK_METHOD_NONE;
    };
}

auto from_libinput_click_method(libinput_config_click_method method)
{
    switch (method) {
    case LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS:
        return control::clicks::button_areas;
    case LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER:
        return control::clicks::finger_count;
    case LIBINPUT_CONFIG_CLICK_METHOD_NONE:
    default:
        return control::clicks::none;
    };
}

bool pointer_control::supports_click_method(control::clicks method) const
{
    auto methods = libinput_device_config_click_get_methods(dev);
    return to_libinput_click_method(method) & methods;
}

control::clicks pointer_control::default_click_method() const
{
    auto method = libinput_device_config_click_get_default_method(dev);
    return from_libinput_click_method(method);
}

control::clicks pointer_control::click_method() const
{
    auto method = libinput_device_config_click_get_method(dev);
    return from_libinput_click_method(method);
}

bool pointer_control::set_click_method_impl(control::clicks method)
{
    auto val = to_libinput_click_method(method);
    return libinput_device_config_click_set_method(dev, val) == LIBINPUT_CONFIG_STATUS_SUCCESS;
}

}
