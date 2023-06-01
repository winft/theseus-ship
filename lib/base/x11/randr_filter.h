/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "event_filter.h"
#include "xcb/extensions.h"
#include "xcb/randr.h"

#include <QTimer>
#include <memory>
#include <xcb/xcb.h>

namespace KWin::base::x11
{

template<typename Platform>
class randr_filter : public event_filter
{
public:
    explicit randr_filter(Platform& platform)
        : event_filter(*platform.x11_event_filters, xcb::extensions::self()->randr_notify_event())
        , platform(platform)
        , changed_timer(std::make_unique<QTimer>())
    {
        changed_timer->setSingleShot(true);
        changed_timer->setInterval(100);
        QObject::connect(
            changed_timer.get(), &QTimer::timeout, &platform, &Platform::update_outputs);
    }

    bool event(xcb_generic_event_t* event) override
    {
        Q_ASSERT((event->response_type & ~0x80) == xcb::extensions::self()->randr_notify_event());

        // update default screen
        auto* xrrEvent = reinterpret_cast<xcb_randr_screen_change_notify_event_t*>(event);
        auto screen = get_default_screen(platform.x11_data);

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
        changed_timer->start();

        return false;
    }

private:
    Platform& platform;
    std::unique_ptr<QTimer> changed_timer;
};

}
