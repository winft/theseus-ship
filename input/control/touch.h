/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "device.h"

#include <kwin_export.h>

namespace KWin::input::control
{

class KWIN_EXPORT touch : public device
{
    Q_OBJECT
public:
    explicit touch(platform* plat);

    virtual bool supports_gesture() const = 0;
    virtual QSizeF size() const = 0;

    virtual bool supports_calibration_matrix() const = 0;
    virtual QMatrix4x4 default_calibration_matrix() const = 0;

    std::string output_name() const;

    void set_orientation(Qt::ScreenOrientation orientation);

protected:
    virtual bool set_orientation_impl(float matrix[6]) = 0;
};

}
