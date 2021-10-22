/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "virtualdesktops.h"
#include "win/stacking_order.h"
#include "workspace.h"

namespace KWin
{
namespace input::wayland
{

template<typename Dev>
void device_redirect_init(Dev* dev)
{
    QObject::connect(workspace()->stacking_order, &win::stacking_order::changed, dev, &Dev::update);
    QObject::connect(workspace(), &Workspace::clientMinimizedChanged, dev, &Dev::update);
    QObject::connect(
        VirtualDesktopManager::self(), &VirtualDesktopManager::currentChanged, dev, &Dev::update);
}

}
}
