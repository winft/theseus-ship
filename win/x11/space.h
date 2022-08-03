/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "desktop_space.h"
#include "netinfo.h"
#include "screen_edge.h"
#include "screen_edges_filter.h"
#include "space_areas.h"
#include "space_setup.h"
#include "window.h"

#include "base/x11/xcb/helpers.h"
#include "input/x11/platform.h"
#include "input/x11/redirect.h"
#include "utils/blocker.h"
#include "win/desktop_space.h"
#include "win/screen_edges.h"
#include "win/space.h"
#include "win/stacking_order.h"

#include <vector>

namespace KWin::win::x11
{

template<typename Base>
class space : public win::space
{
public:
    using x11_window = window;

    space(Base& base)
        : win::space(*base.render->compositor)
        , base{base}
    {
        win::init_space(*this);

        if (base.input) {
            this->input = std::make_unique<input::x11::redirect>(*base.input, *this);
        }

        atoms = std::make_unique<base::x11::atoms>(connection());
        edges = std::make_unique<win::screen_edger>(*this);
        dbus = std::make_unique<base::dbus::kwin_impl<win::space, input::platform>>(
            *this, base.input.get());

        QObject::connect(virtual_desktop_manager->qobject.get(),
                         &virtual_desktop_manager_qobject::desktopRemoved,
                         qobject.get(),
                         [this] {
                             auto const desktop_count
                                 = static_cast<int>(virtual_desktop_manager->count());
                             for (auto const& window : windows) {
                                 if (!window->control) {
                                     continue;
                                 }
                                 if (window->isOnAllDesktops()) {
                                     continue;
                                 }
                                 if (window->desktop() <= desktop_count) {
                                     continue;
                                 }
                                 send_window_to_desktop(*this, window, desktop_count, true);
                             }
                         });

        QObject::connect(&base, &base::platform::topology_changed, qobject.get(), [this] {
            if (!this->render.scene) {
                return;
            }
            // desktopResized() should take care of when the size or
            // shape of the desktop has changed, but we also want to
            // catch refresh rate changes
            //
            // TODO: is this still necessary since we get the maximal refresh rate now
            // dynamically?
            this->render.reinitialize();
        });

        x11::init_space(*this);
    }

    void resize(QSize const& size) override
    {
        handle_desktop_resize(size);
        win::handle_desktop_resize(*this, size);
    }

    Toplevel* findInternal(QWindow* window) const override
    {
        if (!window) {
            return nullptr;
        }
        return find_unmanaged<win::x11::window>(*this, window->winId());
    }

    win::screen_edge* create_screen_edge(win::screen_edger& edger) override
    {
        if (!edges_filter) {
            edges_filter = std::make_unique<screen_edges_filter>(*this);
        }
        return new screen_edge(&edger, *atoms);
    }

    void update_space_area_from_windows(QRect const& desktop_area,
                                        std::vector<QRect> const& screens_geos,
                                        win::space_areas& areas) override
    {
        for (auto const& window : windows) {
            if (!window->control) {
                continue;
            }
            if (auto x11_window = qobject_cast<win::x11::window*>(window)) {
                update_space_areas(x11_window, desktop_area, screens_geos, areas);
            }
        }
    }

    Base& base;

private:
    std::unique_ptr<base::x11::event_filter> edges_filter;
};

/**
 * Some fullscreen effects have to raise the screenedge on top of an input window, thus all windows
 * this function puts them back where they belong for regular use and is some cheap variant of
 * the regular propagate_clients function in that it completely ignores managed clients and
 * everything else and also does not update the NETWM property. Called from
 * Effects::destroyInputWindow so far.
 */
template<typename Space>
void stack_screen_edges_under_override_redirect(Space* space)
{
    if (!rootInfo()) {
        return;
    }

    std::vector<xcb_window_t> windows;
    windows.push_back(rootInfo()->supportWindow());

    auto const edges_wins = space->edges->windows();
    windows.insert(windows.end(), edges_wins.begin(), edges_wins.end());

    base::x11::xcb::restack_windows(windows);
}

}
