/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "touch.h"

#include "config.h"

#include <QMatrix4x4>

namespace KWin::input::control
{

touch::touch(platform* plat)
    : device(new device_config, plat)
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
    static QMatrix4x4 const portraitMatrix{
        0.0f, -1.0f, 1.0f, 0.0f,
        1.0f,  0.0f, 0.0f, 0.0f,
        0.0f,  0.0f, 1.0f, 0.0f,
        0.0f,  0.0f, 0.0f, 1.0f
    };

    // 180 deg cw:
    static QMatrix4x4 const invertedLandscapeMatrix{
        -1.0f,  0.0f, 1.0f, 0.0f,
         0.0f, -1.0f, 1.0f, 0.0f,
         0.0f,  0.0f, 1.0f, 0.0f,
         0.0f,  0.0f, 0.0f, 1.0f
    };

    // 270 deg cw
    static QMatrix4x4 const invertedPortraitMatrix{
         0.0f, 1.0f, 0.0f, 0.0f,
        -1.0f, 0.0f, 1.0f, 0.0f,
         0.0f,  0.0f, 1.0f, 0.0f,
         0.0f,  0.0f, 0.0f, 1.0f
    };
    // clang-format on

    QMatrix4x4 matrix;
    switch (orientation) {
    case Qt::PortraitOrientation:
        matrix = portraitMatrix;
        break;
    case Qt::InvertedLandscapeOrientation:
        matrix = invertedLandscapeMatrix;
        break;
    case Qt::InvertedPortraitOrientation:
        matrix = invertedPortraitMatrix;
        break;
    case Qt::PrimaryOrientation:
    case Qt::LandscapeOrientation:
    default:
        break;
    }

    auto const combined = default_calibration_matrix() * matrix;
    auto const columnOrder = combined.constData();

    // clang-format off
    float m[6] = {
        columnOrder[0], columnOrder[4], columnOrder[8],
        columnOrder[1], columnOrder[5], columnOrder[9]
    };
    // clang-format on

    set_orientation_impl(m);
}

}
