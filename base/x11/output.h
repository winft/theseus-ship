/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/output.h"

#include "render/backend/x11/output.h"

#include <xcb/randr.h>

namespace KWin::base::x11
{

class platform;

class KWIN_EXPORT output : public base::output
{
    Q_OBJECT
public:
    output(x11::platform& platform);
    QString name() const override;

    QRect geometry() const override;

    int refresh_rate() const override;

    int gamma_ramp_size() const override;
    bool set_gamma_ramp(gamma_ramp const& gamma) override;

    QSize physical_size() const override;

    struct {
        QString name;
        QRect geometry;
        QSize physical_size;
        int gamma_ramp_size{0};
        int refresh_rate{-1};
        xcb_randr_crtc_t crtc{XCB_NONE};
    } data;

    render::backend::x11::output render;

private:
    x11::platform& platform;
};

}
