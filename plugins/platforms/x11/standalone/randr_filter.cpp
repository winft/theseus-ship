/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "randr_filter.h"

#include "atoms.h"
#include "screens.h"
#include "x11_platform.h"

#include <xcb/xcb.h>

#include <QTimer>

namespace KWin
{

RandrFilter::RandrFilter(X11StandalonePlatform* backend)
    : X11EventFilter(Xcb::Extensions::self()->randrNotifyEvent())
    , m_backend(backend)
    , m_changedTimer(new QTimer(backend))
{
    m_changedTimer->setSingleShot(true);
    m_changedTimer->setInterval(100);
    QObject::connect(m_changedTimer, &QTimer::timeout, Screens::self(), &Screens::updateAll);
}

bool RandrFilter::event(xcb_generic_event_t* event)
{
    Q_ASSERT((event->response_type & ~0x80) == Xcb::Extensions::self()->randrNotifyEvent());

    m_backend->updateOutputs();

    // Let's try to gather a few XRandR events, unlikely that there is just one.
    m_changedTimer->start();

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

    return false;
}

}
