/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <desktop/screen_locker_watcher.h>

#include <QObject>
#include <memory>

namespace KWin::desktop
{

class platform
{
public:
    template<typename Space>
    platform(Space& space)
        : screen_locker_watcher{std::make_unique<desktop::screen_locker_watcher>()}
    {
        QObject::connect(screen_locker_watcher.get(),
                         &desktop::screen_locker_watcher::locked,
                         space.qobject.get(),
                         &Space::qobject_t::screen_locked);
        ;
    }

    virtual ~platform() = default;

    std::unique_ptr<desktop::screen_locker_watcher> screen_locker_watcher;
};

}
