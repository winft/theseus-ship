/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "touch.h"

#include "config.h"

#include <QMatrix4x4>

namespace KWin::input::control
{

touch::touch()
    : device(new device_config)
{
}

std::string touch::output_name() const
{
    // TODO(romangg)
    return "";
}

void touch::set_orientation(Qt::ScreenOrientation orientation)
{
    if (!supports_calibration_matrix()) {
        return;
    }

    // clang-format off
    // 90 deg cw:
    static QMatrix4x4 const portrait_matrix{
        0.0f, -1.0f, 1.0f, 0.0f,
        1.0f,  0.0f, 0.0f, 0.0f,
        0.0f,  0.0f, 1.0f, 0.0f,
        0.0f,  0.0f, 0.0f, 1.0f
    };

    // 180 deg cw:
    static QMatrix4x4 const inverted_landscape_matrix{
        -1.0f,  0.0f, 1.0f, 0.0f,
         0.0f, -1.0f, 1.0f, 0.0f,
         0.0f,  0.0f, 1.0f, 0.0f,
         0.0f,  0.0f, 0.0f, 1.0f
    };

    // 270 deg cw
    static QMatrix4x4 const inverted_portrait_matrix{
         0.0f, 1.0f, 0.0f, 0.0f,
        -1.0f, 0.0f, 1.0f, 0.0f,
         0.0f,  0.0f, 1.0f, 0.0f,
         0.0f,  0.0f, 0.0f, 1.0f
    };
    // clang-format on

    QMatrix4x4 matrix;
    switch (orientation) {
    case Qt::PortraitOrientation:
        matrix = portrait_matrix;
        break;
    case Qt::InvertedLandscapeOrientation:
        matrix = inverted_landscape_matrix;
        break;
    case Qt::InvertedPortraitOrientation:
        matrix = inverted_portrait_matrix;
        break;
    case Qt::PrimaryOrientation:
    case Qt::LandscapeOrientation:
    default:
        break;
    }

    set_orientation_impl(default_calibration_matrix() * matrix);
}

}
