/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "space.h"

#include "xdg_activation.h"

#include "screens.h"
#include "wayland_server.h"
#include "win/input.h"
#include "win/stacking_order.h"
#include "win/x11/stacking_tree.h"

#ifdef KWIN_BUILD_TABBOX
#include "tabbox.h"
#endif

namespace KWin::win::wayland
{

space::space()
{
    activation.reset(new win::wayland::xdg_activation);
    QObject::connect(this, &Workspace::clientActivated, this, [this] {
        if (activeClient()) {
            activation->clear();
        }
    });

    QObject::connect(
        waylandServer(), &WaylandServer::window_added, this, [this](win::wayland::window* window) {
            assert(!contains(m_windows, window));

            if (window->control && !window->layer_surface) {
                setupClientConnections(window);
                window->updateDecoration(false);
                win::update_layer(window);

                auto const area
                    = clientArea(PlacementArea, Screens::self()->current(), window->desktop());
                auto placementDone = false;

                if (window->isInitialPositionSet()) {
                    placementDone = true;
                }
                if (window->control->fullscreen()) {
                    placementDone = true;
                }
                if (window->maximizeMode() == win::maximize_mode::full) {
                    placementDone = true;
                }
                if (window->control->rules().checkPosition(invalidPoint, true) != invalidPoint) {
                    placementDone = true;
                }
                if (!placementDone) {
                    window->placeIn(area);
                }

                m_allClients.push_back(window);
            }

            m_windows.push_back(window);

            if (!contains(stacking_order->pre_stack, window)) {
                // Raise if it hasn't got any stacking position yet.
                stacking_order->pre_stack.push_back(window);
            }
            if (!contains(stacking_order->sorted(), window)) {
                // It'll be updated later, and updateToolWindows() requires window to be in
                // stacking_order.
                stacking_order->win_stack.push_back(window);
            }

            x_stacking_tree->mark_as_dirty();
            stacking_order->update(true);

            if (window->control) {
                updateClientArea();

                if (window->wantsInput() && !window->control->minimized()) {
                    activateClient(window);
                }

                updateTabbox();

                QObject::connect(window, &win::wayland::window::windowShown, this, [this, window] {
                    win::update_layer(window);
                    x_stacking_tree->mark_as_dirty();
                    stacking_order->update(true);
                    updateClientArea();
                    if (window->wantsInput()) {
                        activateClient(window);
                    }
                });
                QObject::connect(window, &win::wayland::window::windowHidden, this, [this] {
                    // TODO: update tabbox if it's displayed
                    x_stacking_tree->mark_as_dirty();
                    stacking_order->update(true);
                    updateClientArea();
                });
            }
            Q_EMIT wayland_window_added(window);
        });

    QObject::connect(waylandServer(),
                     &WaylandServer::window_removed,
                     this,
                     [this](win::wayland::window* window) {
                         remove_all(m_windows, window);

                         if (window->control) {
                             remove_all(m_allClients, window);
                             if (window == most_recently_raised) {
                                 most_recently_raised = nullptr;
                             }
                             if (window == delayfocus_client) {
                                 cancelDelayFocus();
                             }
                             if (window == last_active_client) {
                                 last_active_client = nullptr;
                             }
                             if (window == client_keys_client) {
                                 setupWindowShortcutDone(false);
                             }
                             if (!window->control->shortcut().isEmpty()) {
                                 // Remove from client_keys.
                                 win::set_shortcut(window, QString());
                             }
                             clientHidden(window);
                             Q_EMIT clientRemoved(window);
                         }

                         x_stacking_tree->mark_as_dirty();
                         stacking_order->update(true);

                         if (window->control) {
                             updateClientArea();
                             updateTabbox();
                         }
                     });
}

space::~space()
{
    stacking_order->lock();

    // TODO(romangg): Do we really need both loops?
    auto const windows = waylandServer()->windows;
    for (auto win : windows) {
        win->destroy();
        remove_all(m_windows, win);
    }

    for (auto const& window : m_windows) {
        if (auto win = qobject_cast<win::wayland::window*>(window)) {
            win->destroy();
            remove_all(m_windows, win);
        }
    }
}

}
