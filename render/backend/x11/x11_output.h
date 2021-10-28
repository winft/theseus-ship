/*
    SPDX-FileCopyrightText: 2019 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_X11_OUTPUT_H
#define KWIN_X11_OUTPUT_H

#include "base/output.h"
#include <kwin_export.h>

#include <QObject>
#include <QRect>

#include <xcb/randr.h>

namespace KWin::render::backend::x11
{

/**
 * X11 output representation
 */
class KWIN_EXPORT X11Output : public base::output
{
    Q_OBJECT

public:
    QString name() const override;
    void set_name(QString set);

    QRect geometry() const override;
    void set_geometry(QRect set);

    int refresh_rate() const override;
    void set_refresh_rate(int set);

    int gamma_ramp_size() const override;
    bool set_gamma_ramp(base::gamma_ramp const& gamma) override;

    QSize physical_size() const override;
    void set_physical_size(QSize const& size);

private:
    void set_crtc(xcb_randr_crtc_t crtc);
    void set_gamma_ramp_size(int size);

    xcb_randr_crtc_t m_crtc = XCB_NONE;
    QString m_name;
    QRect m_geometry;
    QSize m_physical_size;
    int m_gamma_ramp_size;
    int m_refresh_rate;

    friend class X11StandalonePlatform;
};

}

#endif
