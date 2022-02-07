/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "space.h"

#include "screen_edge.h"
#include "screen_edges_filter.h"
#include "space_areas.h"
#include "space_setup.h"
#include "window.h"

namespace KWin::win::x11
{

space::space()
{
    edges = std::make_unique<win::screen_edger>(*this);

    QObject::connect(
        virtual_desktop_manager::self(), &virtual_desktop_manager::desktopRemoved, this, [this] {
            auto const desktop_count = static_cast<int>(virtual_desktop_manager::self()->count());
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

    init_space(*this);
}

space::~space()
{
}

win::screen_edge* space::create_screen_edge(win::screen_edger& edger)
{
    if (!edges_filter) {
        edges_filter = std::make_unique<screen_edges_filter>();
    }
    return new screen_edge(&edger);
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
