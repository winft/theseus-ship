/*
    SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "subspace_manager.h"

namespace KWin::win
{

subspace_manager_qobject::subspace_manager_qobject() = default;

subspace_manager::subspace_manager()
    : qobject{std::make_unique<subspace_manager_qobject>()}
    , singleton{subspace_manager_create_singleton(*this)}

{
    singleton_interface::subspaces = singleton.get();

    swipe_gesture.released_x = std::make_unique<QAction>();
    swipe_gesture.released_y = std::make_unique<QAction>();
}

subspace_manager::~subspace_manager()
{
    singleton_interface::subspaces = {};
}

}
