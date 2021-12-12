/*
    SPDX-FileCopyrightText: 2019 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "output.h"

#include "base/gamma_ramp.h"
#include "base/platform.h"
#include "main.h"
#include "screens.h"

namespace KWin::render::backend::x11
{

QString output::name() const
{
    return m_name;
}

void output::set_name(QString set)
{
    m_name = set;
}

QRect output::geometry() const
{
    if (m_geometry.isValid()) {
        return m_geometry;
    }

    // xinerama, lacks RandR
    return QRect(QPoint(0, 0), kwinApp()->get_base().screens.displaySize());
}

void output::set_geometry(QRect set)
{
    m_geometry = set;
}

int output::refresh_rate() const
{
    return m_refresh_rate;
}

void output::set_refresh_rate(int set)
{
    m_refresh_rate = set;
}

int output::gamma_ramp_size() const
{
    return m_gamma_ramp_size;
}

bool output::set_gamma_ramp(base::gamma_ramp const& gamma)
{
    if (m_crtc == XCB_NONE) {
        return false;
    }

    xcb_randr_set_crtc_gamma(
        connection(), m_crtc, gamma.size(), gamma.red(), gamma.green(), gamma.blue());

    return true;
}

void output::set_crtc(xcb_randr_crtc_t crtc)
{
    m_crtc = crtc;
}

void output::set_gamma_ramp_size(int size)
{
    m_gamma_ramp_size = size;
}

QSize output::physical_size() const
{
    return m_physical_size;
}

void output::set_physical_size(QSize const& size)
{
    m_physical_size = size;
}

}
