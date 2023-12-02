/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <base/platform_helpers.h>
#include <base/seat/backend/wlroots/session.h>

#include <QApplication>

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

template<typename Platform>
void platform_start(Platform& platform)
{
    platform.backend.start();
    platform.mod.render->start(*platform.mod.space);
    platform.mod.space->input->pointer->warp(QRect({}, platform.topology.size).center());
}

template<typename Platform>
int exec(Platform& platform, QApplication& app)
{
    platform_start(platform);
    return app.exec();
}

}
