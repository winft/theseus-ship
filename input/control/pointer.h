/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "device.h"

namespace KWin::input::control
{
class pointer_config;

enum class accel_profile {
    none,
    flat,
    adaptive,
};

enum class scroll {
    none,
    two_finger,
    edge,
    on_button_down,
};

enum class clicks {
    none,
    button_areas,
    finger_count,
};

class KWIN_EXPORT pointer : public device
{
    Q_OBJECT
public:
    explicit pointer(platform* plat);

    virtual bool is_touchpad() const = 0;
    virtual bool supports_gesture() const = 0;

    virtual QSizeF size() const = 0;
    virtual Qt::MouseButtons supported_buttons() const = 0;

    virtual int tap_finger_count() const = 0;
    virtual bool tap_to_click_enabled_by_default() = 0;
    virtual bool is_tap_to_click() const = 0;
    void set_tap_to_click(bool set);

    virtual bool tap_and_drag_enabled_by_default() const = 0;
    virtual bool is_tap_and_drag() const = 0;
    void set_tap_and_drag(bool set);
    virtual bool tap_drag_lock_enabled_by_default() const = 0;
    virtual bool is_tap_drag_lock() const = 0;
    void set_tap_drag_lock(bool set);

    virtual bool supports_disable_events_on_external_mouse() const = 0;
    virtual bool supports_disable_while_typing() const = 0;
    virtual bool disable_while_typing_enabled_by_default() const = 0;

    virtual bool supports_acceleration() const = 0;
    virtual bool supports_left_handed() const = 0;
    virtual bool left_handed_enabled_by_default() const = 0;

    virtual bool supports_middle_emulation() const = 0;
    virtual bool supports_natural_scroll() const = 0;
    virtual bool supports_scroll_method(scroll method) const = 0;

    virtual bool middle_emulation_enabled_by_default() const = 0;
    virtual bool natural_scroll_enabled_by_default() const = 0;

    virtual scroll default_scroll_method() const = 0;
    virtual scroll scroll_method() const = 0;
    void set_scroll_method(scroll method);

    virtual bool supports_lmr_tap_button_map() const = 0;
    virtual bool lmr_tap_button_map_enabled_by_default() const = 0;

    void set_lmr_tap_button_map(bool set);
    virtual bool lmr_tap_button_map() const = 0;

    virtual bool is_middle_emulation() const = 0;
    void set_middle_emulation(bool set);

    virtual uint32_t default_scroll_button() const = 0;
    virtual bool is_natural_scroll() const = 0;
    void set_natural_scroll(bool set);

    virtual uint32_t scroll_button() const = 0;
    void set_scroll_button(uint32_t button);

    double default_scroll_factor() const;
    double get_scroll_factor() const;
    void set_scroll_factor(double factor);

    void set_disable_while_typing(bool set);
    virtual bool is_disable_while_typing() const = 0;
    virtual bool is_left_handed() const = 0;
    void set_left_handed(bool set);

    virtual double default_acceleration() const = 0;
    virtual double acceleration() const = 0;

    void set_acceleration(double acceleration);

    virtual bool supports_acceleration_profile(accel_profile profile) const = 0;
    virtual accel_profile default_acceleration_profile() const = 0;
    virtual accel_profile acceleration_profile() const = 0;
    void set_acceleration_profile(accel_profile profile);

    virtual bool supports_click_method(clicks method) const = 0;
    virtual clicks default_click_method() const = 0;
    virtual clicks click_method() const = 0;
    void set_click_method(clicks method);

    pointer_config* config;

protected:
    virtual bool set_tap_to_click_impl(bool active) = 0;
    virtual bool set_tap_and_drag_impl(bool active) = 0;
    virtual bool set_tap_drag_lock_impl(bool active) = 0;
    virtual bool set_scroll_method_impl(scroll method) = 0;
    virtual bool set_lmr_tap_button_map_impl(bool active) = 0;
    virtual bool set_middle_emulation_impl(bool active) = 0;
    virtual bool set_natural_scroll_impl(bool active) = 0;
    virtual bool set_scroll_button_impl(uint32_t button) = 0;
    virtual bool set_disable_while_typing_impl(bool active) = 0;
    virtual bool set_left_handed_impl(bool active) = 0;
    virtual bool set_acceleration_impl(double acceleration) = 0;
    virtual bool set_acceleration_profile_impl(accel_profile profile) = 0;
    virtual bool set_click_method_impl(clicks method) = 0;

Q_SIGNALS:
    void tap_button_map_changed();
    void left_handed_changed();
    void disable_while_typing_changed();
    void acceleration_changed();
    void acceleration_profile_changed();
    void tap_to_click_changed();
    void tap_and_drag_changed();
    void tap_drag_lock_changed();
    void middle_emulation_changed();
    void natural_scroll_changed();
    void scroll_method_changed();
    void scroll_button_changed();
    void scroll_factor_changed();
    void click_method_changed();

private:
    double scroll_factor{1.};
};

}
