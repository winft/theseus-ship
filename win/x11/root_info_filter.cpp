/*
    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "root_info_filter.h"

#include "virtualdesktops.h"
#include "win/x11/netinfo.h"

namespace KWin::win::x11
{

root_info_filter::root_info_filter(root_info* info)
    : base::x11::event_filter(QVector<int>{XCB_PROPERTY_NOTIFY, XCB_CLIENT_MESSAGE})
    , info{info}
{
}

bool root_info_filter::event(xcb_generic_event_t* event)
{
    NET::Properties dirtyProtocols;
    NET::Properties2 dirtyProtocols2;
    info->event(event, &dirtyProtocols, &dirtyProtocols2);

    if (dirtyProtocols & NET::DesktopNames) {
        virtual_desktop_manager::self()->save();
    }
    if (dirtyProtocols2 & NET::WM2DesktopLayout) {
        virtual_desktop_manager::self()->updateLayout();
    }
    return false;
}

}
