/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "group.h"
#include "hide.h"
#include "netinfo.h"
#include "window.h"

#include "base/output_helpers.h"
#include "base/platform.h"
#include "main.h"
#include "toplevel.h"

#include <deque>

namespace KWin::win::x11
{

template<typename Order>
void propagate_clients(Order& order, bool propagate_new_clients)
{
    if (!rootInfo()) {
        return;
    }
    // restack the windows according to the stacking order
    // supportWindow > electric borders > clients > hidden clients
    std::vector<xcb_window_t> new_win_stack;

    // Stack all windows under the support window. The support window is
    // not used for anything (besides the NETWM property), and it's not shown,
    // but it was lowered after kwin startup. Stacking all clients below
    // it ensures that no client will be ever shown above override-redirect
    // windows (e.g. popups).
    new_win_stack.push_back(rootInfo()->supportWindow());

    auto const edges_wins = order.space.edges->windows();
    new_win_stack.insert(new_win_stack.end(), edges_wins.begin(), edges_wins.end());
    new_win_stack.insert(
        new_win_stack.end(), order.manual_overlays.begin(), order.manual_overlays.end());

    // Twice the stacking-order size for inputWindow
    new_win_stack.reserve(new_win_stack.size() + 2 * order.win_stack.size());

    // TODO use ranges::view and ranges::transform in c++20
    std::vector<xcb_window_t> hidden_windows;
    std::for_each(order.win_stack.rbegin(),
                  order.win_stack.rend(),
                  [&new_win_stack, &hidden_windows](auto window) {
                      auto x11_window = qobject_cast<x11::window*>(window);
                      if (!x11_window) {
                          return;
                      }

                      // Hidden windows with preview are windows that should be unmapped but is kept
                      // for compositing ensure they are stacked below everything else (as far as
                      // pure X stacking order is concerned).
                      if (hidden_preview(x11_window)) {
                          hidden_windows.push_back(x11_window->frameId());
                          return;
                      }

                      // Stack the input window above the frame
                      if (x11_window->xcb_windows.input) {
                          new_win_stack.push_back(x11_window->xcb_windows.input);
                      }

                      new_win_stack.push_back(x11_window->frameId());
                  });

    // when having hidden previews, stack hidden windows below everything else
    // (as far as pure X stacking order is concerned), in order to avoid having
    // these windows that should be unmapped to interfere with other windows.
    std::copy(hidden_windows.begin(), hidden_windows.end(), std::back_inserter(new_win_stack));

    // TODO isn't it too inefficient to restack always all clients?
    // TODO don't restack not visible windows?
    Q_ASSERT(new_win_stack.at(0) == rootInfo()->supportWindow());
    base::x11::xcb::restack_windows(new_win_stack);

    if (propagate_new_clients) {
        // TODO this is still not completely in the map order
        // TODO use ranges::view and ranges::transform in c++20
        std::vector<xcb_window_t> xcb_windows;
        std::vector<xcb_window_t> non_desktops;
        std::copy(order.manual_overlays.begin(),
                  order.manual_overlays.end(),
                  std::back_inserter(xcb_windows));

        for (auto const& window : order.space.m_windows) {
            if (!window->control) {
                continue;
            }

            auto x11_window = qobject_cast<x11::window*>(window);
            if (!x11_window) {
                continue;
            }

            if (is_desktop(x11_window)) {
                xcb_windows.push_back(x11_window->xcb_window);
            } else {
                non_desktops.push_back(x11_window->xcb_window);
            }
        }

        /// Desktop windows are always on the bottom, so copy the non-desktop windows to the
        /// end/top.
        std::copy(non_desktops.begin(), non_desktops.end(), std::back_inserter(xcb_windows));
        rootInfo()->setClientList(xcb_windows.data(), xcb_windows.size());
    }

    std::vector<xcb_window_t> stacked_xcb_windows;

    for (auto window : order.win_stack) {
        if (auto x11_window = qobject_cast<x11::window*>(window)) {
            stacked_xcb_windows.push_back(x11_window->xcb_window);
        }
    }

    std::copy(order.manual_overlays.begin(),
              order.manual_overlays.end(),
              std::back_inserter(stacked_xcb_windows));
    rootInfo()->setClientListStacking(stacked_xcb_windows.data(), stacked_xcb_windows.size());
}

}
