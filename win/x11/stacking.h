/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "focus_stealing.h"
#include "group.h"
#include "hide.h"
#include "netinfo.h"
#include "window.h"
#include "window_find.h"

#include "base/output_helpers.h"
#include "base/platform.h"
#include "main.h"
#include "toplevel.h"
#include "win/activation.h"

#include <deque>

namespace KWin::win::x11
{

template<typename Win, typename Space>
std::vector<Win*> get_unmanageds(Space const& space)
{
    std::vector<Win*> ret;
    for (auto const& window : space.windows) {
        if (window->xcb_window && !window->control && !window->remnant) {
            ret.push_back(window);
        }
    }
    return ret;
}

template<typename Space>
void render_stack_unmanaged_windows(Space& space)
{
    if (!kwinApp()->x11Connection()) {
        return;
    }

    auto xcbtree = std::make_unique<base::x11::xcb::tree>(kwinApp()->x11RootWindow());
    if (xcbtree->is_null()) {
        return;
    }

    // this constructs a vector of references with the start and end
    // of the xcbtree C pointer array of type xcb_window_t, we use reference_wrapper to only
    // create an vector of references instead of making a copy of each element into the vector.
    std::vector<std::reference_wrapper<xcb_window_t>> windows(
        xcbtree->children(), xcbtree->children() + xcbtree->data()->children_len);
    auto const& unmanaged_list = get_unmanageds<Toplevel>(space);

    for (auto const& win : windows) {
        auto unmanaged = std::find_if(unmanaged_list.cbegin(),
                                      unmanaged_list.cend(),
                                      [&win](auto u) { return win.get() == u->xcb_window; });

        if (unmanaged != std::cend(unmanaged_list)) {
            space.stacking_order->render_overlays.push_back(*unmanaged);
        }
    }
}

template<typename Space>
void propagate_clients(Space& space, bool propagate_new_clients)
{
    if (!rootInfo()) {
        return;
    }

    auto& order = *space.stacking_order;

    // restack the windows according to the stacking order
    // supportWindow > electric borders > clients > hidden clients
    std::vector<xcb_window_t> stack;

    // Stack all windows under the support window. The support window is
    // not used for anything (besides the NETWM property), and it's not shown,
    // but it was lowered after kwin startup. Stacking all clients below
    // it ensures that no client will be ever shown above override-redirect
    // windows (e.g. popups).
    stack.push_back(rootInfo()->supportWindow());

    auto const edges_wins = space.edges->windows();
    stack.insert(stack.end(), edges_wins.begin(), edges_wins.end());
    stack.insert(stack.end(), order.manual_overlays.begin(), order.manual_overlays.end());

    // Twice the stacking-order size for inputWindow
    stack.reserve(stack.size() + 2 * order.stack.size());

    // TODO use ranges::view and ranges::transform in c++20
    std::vector<xcb_window_t> hidden_windows;
    std::for_each(order.stack.rbegin(), order.stack.rend(), [&stack, &hidden_windows](auto window) {
        auto x11_window = dynamic_cast<x11::window*>(window);
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
            stack.push_back(x11_window->xcb_windows.input);
        }

        stack.push_back(x11_window->frameId());
    });

    // when having hidden previews, stack hidden windows below everything else
    // (as far as pure X stacking order is concerned), in order to avoid having
    // these windows that should be unmapped to interfere with other windows.
    std::copy(hidden_windows.begin(), hidden_windows.end(), std::back_inserter(stack));

    // TODO isn't it too inefficient to restack always all clients?
    // TODO don't restack not visible windows?
    Q_ASSERT(stack.at(0) == rootInfo()->supportWindow());
    base::x11::xcb::restack_windows(stack);

    if (propagate_new_clients) {
        // TODO this is still not completely in the map order
        // TODO use ranges::view and ranges::transform in c++20
        std::vector<xcb_window_t> clients;
        std::vector<xcb_window_t> non_desktops;
        std::copy(order.manual_overlays.begin(),
                  order.manual_overlays.end(),
                  std::back_inserter(clients));

        for (auto const& window : space.windows) {
            if (!window->control) {
                continue;
            }

            auto x11_window = dynamic_cast<x11::window*>(window);
            if (!x11_window) {
                continue;
            }

            if (is_desktop(x11_window)) {
                clients.push_back(x11_window->xcb_window);
            } else {
                non_desktops.push_back(x11_window->xcb_window);
            }
        }

        /// Desktop windows are always on the bottom, so copy the non-desktop windows to the
        /// end/top.
        std::copy(non_desktops.begin(), non_desktops.end(), std::back_inserter(clients));
        rootInfo()->setClientList(clients.data(), clients.size());
    }

    std::vector<xcb_window_t> stacked_clients;

    for (auto window : order.stack) {
        if (auto x11_window = dynamic_cast<x11::window*>(window)) {
            stacked_clients.push_back(x11_window->xcb_window);
        }
    }

    std::copy(order.manual_overlays.begin(),
              order.manual_overlays.end(),
              std::back_inserter(stacked_clients));
    rootInfo()->setClientListStacking(stacked_clients.data(), stacked_clients.size());
}

template<typename Space, typename Win>
void lower_client_within_application(Space* space, Win* window)
{
    if (!window) {
        return;
    }

    window->control->cancel_auto_raise();

    blocker block(space->stacking_order);

    remove_all(space->stacking_order->pre_stack, window);

    bool lowered = false;
    // first try to put it below the bottom-most window of the application
    for (auto it = space->stacking_order->pre_stack.begin();
         it != space->stacking_order->pre_stack.end();
         ++it) {
        auto const& client = *it;
        if (!client) {
            continue;
        }
        if (win::belong_to_same_client(client, window)) {
            space->stacking_order->pre_stack.insert(it, window);
            lowered = true;
            break;
        }
    }
    if (!lowered)
        space->stacking_order->pre_stack.push_front(window);
    // ignore mainwindows
}

template<typename Space, typename Win>
void raise_client_within_application(Space* space, Win* window)
{
    if (!window) {
        return;
    }

    window->control->cancel_auto_raise();

    blocker block(space->stacking_order);
    // ignore mainwindows

    // first try to put it above the top-most window of the application
    for (int i = space->stacking_order->pre_stack.size() - 1; i > -1; --i) {
        auto other = space->stacking_order->pre_stack.at(i);
        if (!other) {
            continue;
        }
        if (other == window) {
            // Don't lower it just because it asked to be raised.
            return;
        }
        if (belong_to_same_client(other, window)) {
            remove_all(space->stacking_order->pre_stack, window);
            auto it = find(space->stacking_order->pre_stack, other);
            assert(it != space->stacking_order->pre_stack.end());
            // Insert after the found one.
            space->stacking_order->pre_stack.insert(it + 1, window);
            break;
        }
    }
}

template<typename Space, typename Win>
void raise_client_request(Space* space,
                          Win* c,
                          NET::RequestSource src = NET::FromApplication,
                          xcb_timestamp_t timestamp = 0)
{
    if (src == NET::FromTool || allow_full_window_raising(*space, c, timestamp)) {
        raise_window(space, c);
    } else {
        raise_client_within_application(space, c);
        set_demands_attention(c, true);
    }
}

template<typename Space, typename Win>
void lower_client_request(Space* space,
                          Win* c,
                          NET::RequestSource src,
                          [[maybe_unused]] xcb_timestamp_t /*timestamp*/)
{
    // If the client has support for all this focus stealing prevention stuff,
    // do only lowering within the application, as that's the more logical
    // variant of lowering when application requests it.
    // No demanding of attention here of course.
    if (src == NET::FromTool || !has_user_time_support(c)) {
        lower_window(space, c);
    } else {
        lower_client_within_application(space, c);
    }
}

template<typename Win>
void restack_window(Win* win,
                    xcb_window_t above,
                    int detail,
                    NET::RequestSource src,
                    xcb_timestamp_t timestamp,
                    bool send_event = false)
{
    Win* other = nullptr;
    if (detail == XCB_STACK_MODE_OPPOSITE) {
        other
            = find_controlled_window<win::x11::window>(win->space, predicate_match::window, above);
        if (!other) {
            raise_or_lower_client(&win->space, win);
            return;
        }

        auto it = win->space.stacking_order->stack.cbegin();
        auto end = win->space.stacking_order->stack.cend();

        while (it != end) {
            if (*it == win) {
                detail = XCB_STACK_MODE_ABOVE;
                break;
            } else if (*it == other) {
                detail = XCB_STACK_MODE_BELOW;
                break;
            }
            ++it;
        }
    } else if (detail == XCB_STACK_MODE_TOP_IF) {
        other
            = find_controlled_window<win::x11::window>(win->space, predicate_match::window, above);
        if (other && other->frameGeometry().intersects(win->frameGeometry())) {
            raise_client_request(&win->space, win, src, timestamp);
        }
        return;
    } else if (detail == XCB_STACK_MODE_BOTTOM_IF) {
        other
            = find_controlled_window<win::x11::window>(win->space, predicate_match::window, above);
        if (other && other->frameGeometry().intersects(win->frameGeometry())) {
            lower_client_request(&win->space, win, src, timestamp);
        }
        return;
    }

    if (!other)
        other
            = find_controlled_window<win::x11::window>(win->space, predicate_match::window, above);

    if (other && detail == XCB_STACK_MODE_ABOVE) {
        auto it = win->space.stacking_order->stack.cend();
        auto begin = win->space.stacking_order->stack.cbegin();

        while (--it != begin) {
            if (*it == other) {
                // the other one is top on stack
                // invalidate and force
                it = begin;
                src = NET::FromTool;
                break;
            }
            auto c = dynamic_cast<Win*>(*it);

            if (!c
                || !(is_normal(*it) && c->isShown() && (*it)->isOnCurrentDesktop()
                     && on_screen(*it, win->central_output))) {
                continue;
            }

            if (*(it - 1) == other)
                break; // "it" is the one above the target one, stack below "it"
        }

        if (it != begin && (*(it - 1) == other)) {
            other = dynamic_cast<Win*>(*it);
        } else {
            other = nullptr;
        }
    }

    if (other) {
        restack(&win->space, win, other);
    } else if (detail == XCB_STACK_MODE_BELOW) {
        lower_client_request(&win->space, win, src, timestamp);
    } else if (detail == XCB_STACK_MODE_ABOVE) {
        raise_client_request(&win->space, win, src, timestamp);
    }

    if (send_event) {
        send_synthetic_configure_notify(win, frame_to_client_rect(win, win->frameGeometry()));
    }
}

}
