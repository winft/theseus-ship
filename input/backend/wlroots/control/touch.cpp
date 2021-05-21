/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "touch.h"

#include "control.h"

namespace KWin::input::backend::wlroots
{

touch_control::touch_control(libinput_device* dev, input::platform* plat)
    : input::control::touch(plat)
    , dev{dev}
{
    populate_metadata(this);
}

bool touch_control::supports_disable_events() const
{
    return supports_disable_events_backend(this);
}

bool touch_control::is_enabled() const
{
    return is_enabled_backend(this);
}

bool touch_control::set_enabled_impl(bool enabled)
{
    return set_enabled_backend(this, enabled);
}

QSizeF touch_control::size() const
{
    return size_backend(this);
}

bool touch_control::supports_gesture() const
{
    return libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_GESTURE);
}

bool touch_control::supports_calibration_matrix() const
{
    return libinput_device_config_calibration_has_matrix(dev);
}

QMatrix4x4 touch_control::default_calibration_matrix() const
{
    float matrix[6];

    auto ret = libinput_device_config_calibration_get_default_matrix(dev, matrix);
    if (ret == 0) {
        return QMatrix4x4();
    }

    // clang-format off
    return QMatrix4x4{
        matrix[0], matrix[1], matrix[2], 0.0f,
        matrix[3], matrix[4], matrix[5], 0.0f,
        0.0f,  0.0f, 1.0f, 0.0f,
        0.0f,  0.0f, 0.0f, 1.0f
    };
    // clang-format on
}

bool touch_control::set_orientation_impl(float matrix[6])
{
    return libinput_device_config_calibration_set_matrix(dev, matrix)
        == LIBINPUT_CONFIG_STATUS_SUCCESS;
}

}
