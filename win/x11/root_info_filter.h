/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"

#include <NETWM>

namespace KWin::win::x11
{

template<typename Info>
class root_info_filter : public base::x11::event_filter
{
public:
    explicit root_info_filter(Info* info)
        : base::x11::event_filter(*info->space.base.x11_event_filters,
                                  QVector<int>{XCB_PROPERTY_NOTIFY, XCB_CLIENT_MESSAGE})
        , info{info}
    {
    }

    bool event(xcb_generic_event_t* event) override
    {
        NET::Properties dirtyProtocols;
        NET::Properties2 dirtyProtocols2;
        info->event(event, &dirtyProtocols, &dirtyProtocols2);

        if (dirtyProtocols & NET::DesktopNames) {
            info->space.virtual_desktop_manager->save();
        }
        if (dirtyProtocols2 & NET::WM2DesktopLayout) {
            info->space.virtual_desktop_manager->updateLayout();
        }
        return false;
    }

private:
    Info* info;
};

}
