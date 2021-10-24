/*
    SPDX-FileCopyrightText: 2019 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "x11_output.h"
#include "screens.h"

namespace KWin::render::backend::x11
{

X11Output::X11Output(QObject* parent)
    : AbstractOutput(parent)
{
}

QString X11Output::name() const
{
    return m_name;
}

void X11Output::set_name(QString set)
{
    m_name = set;
}

QRect X11Output::geometry() const
{
    if (m_geometry.isValid()) {
        return m_geometry;
    }
    return QRect(QPoint(0, 0), Screens::self()->displaySize()); // xinerama, lacks RandR
}

void X11Output::set_geometry(QRect set)
{
    m_geometry = set;
}

int X11Output::refresh_rate() const
{
    return m_refresh_rate;
}

void X11Output::set_refresh_rate(int set)
{
    m_refresh_rate = set;
}

int X11Output::gamma_ramp_size() const
{
    return m_gamma_ramp_size;
}

bool X11Output::set_gamma_ramp(const GammaRamp& gamma)
{
    if (m_crtc == XCB_NONE) {
        return false;
    }

    xcb_randr_set_crtc_gamma(
        connection(), m_crtc, gamma.size(), gamma.red(), gamma.green(), gamma.blue());

    return true;
}

void X11Output::set_crtc(xcb_randr_crtc_t crtc)
{
    m_crtc = crtc;
}

void X11Output::set_gamma_ramp_size(int size)
{
    m_gamma_ramp_size = size;
}

QSize X11Output::physical_size() const
{
    return m_physical_size;
}

void X11Output::set_physical_size(const QSize& size)
{
    m_physical_size = size;
}

}
