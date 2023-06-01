/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "touch.h"

#include "control.h"

namespace KWin::input::backend::wlroots
{

touch_control::touch_control(libinput_device* dev, KSharedConfigPtr input_config)
    : dev{dev}
{
    init_device_control(this, input_config);
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

bool touch_control::set_orientation_impl(QMatrix4x4 const& matrix)
{
    auto const columns = matrix.constData();

    // clang-format off
    float libinput_matrix[6] = {
        columns[0], columns[4], columns[8],
        columns[1], columns[5], columns[9]
    };
    // clang-format on

    return libinput_device_config_calibration_set_matrix(dev, libinput_matrix)
        == LIBINPUT_CONFIG_STATUS_SUCCESS;
}

}
