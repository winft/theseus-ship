/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/idle.h"

#include <Wrapland/Server/idle_notify_v1.h>
#include <Wrapland/Server/kde_idle.h>
#include <memory>

namespace KWin::input::wayland
{

static inline void idle_setup_kde_device(input::idle& idle_manager,
                                         Wrapland::Server::kde_idle_timeout* timeout)
{
    auto idle_cb = [timeout] { timeout->idle(); };
    auto resume_cb = [timeout] { timeout->resume(); };

    auto listener = std::make_unique<idle_listener>(timeout->duration(), idle_cb, resume_cb);

    QObject::connect(timeout,
                     &Wrapland::Server::kde_idle_timeout::simulate_user_activity,
                     idle_manager.qobject.get(),
                     [&] { idle_manager.report_activity(); });
    QObject::connect(
        timeout,
        &Wrapland::Server::kde_idle_timeout::resourceDestroyed,
        idle_manager.qobject.get(),
        [&, listener_ptr = listener.get()] { idle_manager.remove_listener(*listener_ptr); });

    idle_manager.add_listener(std::move(listener));
}

static inline void idle_setup_notification(input::idle& idle_manager,
                                           Wrapland::Server::idle_notification_v1* notification)
{
    auto idle_cb = [notification] { notification->idle(); };
    auto resume_cb = [notification] { notification->resume(); };

    auto listener = std::make_unique<idle_listener>(notification->duration(), idle_cb, resume_cb);

    QObject::connect(
        notification,
        &Wrapland::Server::idle_notification_v1::resourceDestroyed,
        idle_manager.qobject.get(),
        [&, listener_ptr = listener.get()] { idle_manager.remove_listener(*listener_ptr); });

    idle_manager.add_listener(std::move(listener));
}

}
