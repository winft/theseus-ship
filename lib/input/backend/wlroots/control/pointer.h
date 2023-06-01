/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/control/pointer.h"

#include <KSharedConfig>
#include <QSizeF>
#include <libinput.h>

namespace KWin::input::backend::wlroots
{

class pointer_control : public input::control::pointer
{
    Q_OBJECT

public:
    pointer_control(libinput_device* dev, KSharedConfigPtr input_config);
    ~pointer_control() override = default;

    bool supports_disable_events() const override;
    bool is_enabled() const override;
    bool set_enabled_impl(bool enabled) override;

    bool is_touchpad() const override;
    bool supports_gesture() const override;

    QSizeF size() const override;
    Qt::MouseButtons supported_buttons() const override;
    int tap_finger_count() const override;
    bool supports_disable_events_on_external_mouse() const override;

    bool tap_to_click_enabled_by_default() override;
    bool is_tap_to_click() const override;
    bool set_tap_to_click_impl(bool active) override;

    bool tap_and_drag_enabled_by_default() const override;
    bool is_tap_and_drag() const override;
    bool set_tap_and_drag_impl(bool active) override;

    bool tap_drag_lock_enabled_by_default() const override;
    bool is_tap_drag_lock() const override;
    bool set_tap_drag_lock_impl(bool active) override;

    bool supports_disable_while_typing() const override;
    bool disable_while_typing_enabled_by_default() const override;
    bool is_disable_while_typing() const override;
    bool set_disable_while_typing_impl(bool active) override;

    bool supports_left_handed() const override;
    bool left_handed_enabled_by_default() const override;
    bool is_left_handed() const override;
    bool set_left_handed_impl(bool active) override;

    bool supports_middle_emulation() const override;
    bool supports_natural_scroll() const override;
    bool supports_scroll_method(control::scroll method) const override;

    bool middle_emulation_enabled_by_default() const override;
    bool natural_scroll_enabled_by_default() const override;

    control::scroll default_scroll_method() const override;
    control::scroll scroll_method() const override;
    bool set_scroll_method_impl(control::scroll method) override;

    bool supports_lmr_tap_button_map() const override;
    bool lmr_tap_button_map_enabled_by_default() const override;
    bool lmr_tap_button_map() const override;
    bool set_lmr_tap_button_map_impl(bool active) override;

    bool is_middle_emulation() const override;
    bool set_middle_emulation_impl(bool active) override;

    uint32_t default_scroll_button() const override;
    bool is_natural_scroll() const override;
    bool set_natural_scroll_impl(bool active) override;

    uint32_t scroll_button() const override;
    bool set_scroll_button_impl(uint32_t button) override;

    bool supports_acceleration() const override;
    double default_acceleration() const override;
    double acceleration() const override;
    bool set_acceleration_impl(double acceleration) override;

    bool supports_acceleration_profile(control::accel_profile profile) const override;
    control::accel_profile default_acceleration_profile() const override;
    control::accel_profile acceleration_profile() const override;
    bool set_acceleration_profile_impl(control::accel_profile profile) override;

    bool supports_click_method(control::clicks method) const override;
    control::clicks default_click_method() const override;
    control::clicks click_method() const override;
    bool set_click_method_impl(control::clicks method) override;

    libinput_device* dev;
    Qt::MouseButtons buttons{Qt::NoButton};
};

}
