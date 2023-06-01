/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/output.h"

#include "render/backend/x11/output.h"
#include "utils/gamma_ramp.h"

#include <xcb/randr.h>

namespace KWin::base::x11
{

template<typename Platform>
class output : public base::output
{
public:
    using type = output<Platform>;

    output(Platform& platform)
        : render{*this}
        , platform{platform}
    {
    }

    QString name() const override
    {
        return data.name;
    }

    QRect geometry() const override
    {
        if (data.geometry.isValid()) {
            return data.geometry;
        }

        // xinerama, lacks RandR
        return QRect({}, platform.topology.size);
    }

    int refresh_rate() const override
    {
        return data.refresh_rate;
    }

    int gamma_ramp_size() const override
    {
        return data.gamma_ramp_size;
    }

    bool set_gamma_ramp(gamma_ramp const& gamma) override
    {
        if (data.crtc == XCB_NONE) {
            return false;
        }

        xcb_randr_set_crtc_gamma(platform.x11_data.connection,
                                 data.crtc,
                                 gamma.size(),
                                 gamma.red(),
                                 gamma.green(),
                                 gamma.blue());

        return true;
    }

    QSize physical_size() const override
    {
        return data.physical_size;
    }

    struct {
        QString name;
        QRect geometry;
        QSize physical_size;
        int gamma_ramp_size{0};
        int refresh_rate{-1};
        xcb_randr_crtc_t crtc{XCB_NONE};
    } data;

    render::backend::x11::output<type> render;

private:
    Platform& platform;
};

}
