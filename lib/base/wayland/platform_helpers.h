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
void platform_cleanup(Platform& platform)
{
    // need to unload all effects prior to destroying X connection as they might do X calls
    if (platform.mod.render->effects) {
        platform.mod.render->effects->unloadAllEffects();
    }

    if constexpr (requires(Platform platform) { platform.mod.xwayland; }) {
        // Kill Xwayland before terminating its connection.
        platform.mod.xwayland = {};
    }
    platform.server->terminateClientConnections();

    // Block compositor to prevent further compositing from crashing with a null workspace.
    // TODO(romangg): Instead we should kill the compositor before that or remove all outputs.
    platform.mod.render->lock();
}

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
