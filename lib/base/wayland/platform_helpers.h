/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <base/platform_helpers.h>
#include <base/seat/backend/wlroots/session.h>

namespace KWin::base::wayland
{

template<typename Platform>
void platform_init(Platform& platform)
{
    auto session = std::make_unique<seat::backend::wlroots::session>(
        platform.backend.wlroots_session, platform.backend.native);
    session->take_control(platform.server->display->native());
    platform.session = std::move(session);

    base::platform_init(platform);
}

}
