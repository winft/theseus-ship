/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <KStartupInfo>
#include <xcb/xcb.h>

namespace KWin::win::x11
{

template<typename Space>
bool check_startup_notification(Space& space,
                                xcb_window_t w,
                                KStartupInfoId& id,
                                KStartupInfoData& data)
{
    return space.startup->checkStartup(w, id, data) == KStartupInfo::Match;
}

}
