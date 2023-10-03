/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "net/net.h"

#include "base/x11/event_filter.h"

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
        net::Properties dirtyProtocols;
        net::Properties2 dirtyProtocols2;
        info->event(event, &dirtyProtocols, &dirtyProtocols2);

        if (dirtyProtocols & net::DesktopNames) {
            info->space.subspace_manager->save();
        }
        if (dirtyProtocols2 & net::WM2DesktopLayout) {
            info->space.subspace_manager->updateLayout();
        }
        return false;
    }

private:
    Info* info;
};

}
