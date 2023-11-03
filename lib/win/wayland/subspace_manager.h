/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <win/singleton_interface.h>
#include <win/subspace.h>
#include <win/subspace_grid.h>
#include <win/subspace_manager.h>
#include <win/subspace_manager_qobject.h>
#include <win/subspaces_get.h>
#include <win/subspaces_set.h>

#include <KSharedConfig>
#include <QAction>
#include <QPointF>
#include <vector>

namespace Wrapland::Server
{
class PlasmaVirtualDesktopManager;
}

namespace KWin::win::wayland
{

class subspace_manager
{
public:
    subspace_manager()
        : qobject{std::make_unique<subspace_manager_qobject>()}
        , singleton{subspace_manager_create_singleton(*this)}

    {
        singleton_interface::subspaces = singleton.get();

        swipe_gesture.released_x = std::make_unique<QAction>();
        swipe_gesture.released_y = std::make_unique<QAction>();
    }

    ~subspace_manager()
    {
        singleton_interface::subspaces = {};
    }

    std::unique_ptr<subspace_manager_qobject> qobject;
    Wrapland::Server::PlasmaVirtualDesktopManager* m_virtualDesktopManagement{nullptr};

    std::vector<subspace*> subspaces;
    uint rows{2};
    subspace_grid grid;
    subspace* current{nullptr};
    bool nav_wraps{false};

    struct {
        std::unique_ptr<QAction> released_x;
        std::unique_ptr<QAction> released_y;
    } swipe_gesture;
    QPointF current_desktop_offset{0, 0};

    KSharedConfig::Ptr config;
    static constexpr size_t max_count{20};

private:
    std::unique_ptr<subspaces_singleton> singleton;
};

}
