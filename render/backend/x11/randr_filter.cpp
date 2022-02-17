/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "randr_filter.h"

#include "base/x11/xcb/extensions.h"
#include "base/x11/xcb/randr.h"
#include "main.h"
#include "platform.h"
#include "screens.h"

#include <xcb/xcb.h>

#include <QTimer>

namespace KWin::render::backend::x11
{

RandrFilter::RandrFilter(x11::platform* platform)
    : base::x11::event_filter(base::x11::xcb::extensions::self()->randr_notify_event())
    , platform(platform)
    , m_changedTimer(new QTimer(platform))
{
    m_changedTimer->setSingleShot(true);
    m_changedTimer->setInterval(100);
    QObject::connect(m_changedTimer, &QTimer::timeout, platform, &platform::update_outputs);
}

bool RandrFilter::event(xcb_generic_event_t* event)
{
    Q_ASSERT((event->response_type & ~0x80)
             == base::x11::xcb::extensions::self()->randr_notify_event());

    // update default screen
    auto* xrrEvent = reinterpret_cast<xcb_randr_screen_change_notify_event_t*>(event);
    xcb_screen_t* screen = defaultScreen();
    if (xrrEvent->rotation & (XCB_RANDR_ROTATION_ROTATE_90 | XCB_RANDR_ROTATION_ROTATE_270)) {
        screen->width_in_pixels = xrrEvent->height;
        screen->height_in_pixels = xrrEvent->width;
        screen->width_in_millimeters = xrrEvent->mheight;
        screen->height_in_millimeters = xrrEvent->mwidth;
    } else {
        screen->width_in_pixels = xrrEvent->width;
        screen->height_in_pixels = xrrEvent->height;
        screen->width_in_millimeters = xrrEvent->mwidth;
        screen->height_in_millimeters = xrrEvent->mheight;
    }

    // Let's try to gather a few XRandR events, unlikely that there is just one.
    m_changedTimer->start();

    return false;
}

}
