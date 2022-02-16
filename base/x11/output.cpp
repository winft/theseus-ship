/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "output.h"

#include "platform.h"

#include "kwinglobals.h"
#include "utils/gamma_ramp.h"

namespace KWin::base::x11
{

output::output(x11::platform& platform)
    : render{*this}
    , platform{platform}
{
}

QString output::name() const
{
    return data.name;
}

QRect output::geometry() const
{
    if (data.geometry.isValid()) {
        return data.geometry;
    }

    // xinerama, lacks RandR
    return QRect({}, platform.topology.size);
}

int output::refresh_rate() const
{
    return data.refresh_rate;
}

int output::gamma_ramp_size() const
{
    return data.gamma_ramp_size;
}

bool output::set_gamma_ramp(gamma_ramp const& gamma)
{
    if (data.crtc == XCB_NONE) {
        return false;
    }

    xcb_randr_set_crtc_gamma(
        connection(), data.crtc, gamma.size(), gamma.red(), gamma.green(), gamma.blue());

    return true;
}

QSize output::physical_size() const
{
    return data.physical_size;
}

}
