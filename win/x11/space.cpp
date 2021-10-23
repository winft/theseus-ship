/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "space.h"

#include "space_areas.h"
#include "window.h"

namespace KWin::win::x11
{

space::space()
{
    QObject::connect(
        VirtualDesktopManager::self(), &VirtualDesktopManager::desktopRemoved, this, [this] {
            auto const desktop_count = static_cast<int>(VirtualDesktopManager::self()->count());
            for (auto const& window : m_allClients) {
                if (window->isOnAllDesktops()) {
                    continue;
                }
                if (window->desktop() <= desktop_count) {
                    continue;
                }
                sendClientToDesktop(window, desktop_count, true);
            }
        });
}

space::~space()
{
}

void space::update_space_area_from_windows(QRect const& desktop_area,
                                           std::vector<QRect> const& screens_geos,
                                           win::space_areas& areas)
{
    for (auto const& window : m_allClients) {
        if (auto x11_window = qobject_cast<win::x11::window*>(window)) {
            update_space_areas(x11_window, desktop_area, screens_geos, areas);
        }
    }
}

}
