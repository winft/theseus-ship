/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "client.h"
#include "focus_stealing.h"
#include "hide.h"
#include "screen_edges.h"
#include "window_find.h"
#include <win/x11/user_time.h>

#include "base/output_helpers.h"
#include "base/x11/xcb/helpers.h"
#include "win/activation.h"

#include <deque>
#include <variant>

namespace KWin::win::x11
{

template<typename Space>
auto get_unmanageds(Space const& space) -> std::vector<typename Space::window_t>
{
    using var_win = typename Space::window_t;
    std::vector<var_win> ret;

    for (auto const& window : space.windows) {
        std::visit(overload{[&](typename Space::x11_window* win) {
                                if (!win->control && !win->remnant) {
                                    ret.push_back(var_win(win));
                                }
                            },
                            [](auto&&) {}},
                   window);
    }

    return ret;
}

template<typename Space>
void render_stack_unmanaged_windows(Space& space)
{
    auto const& x11_data = space.base.x11_data;
    if (!x11_data.connection) {
        return;
    }

    auto xcbtree
        = std::make_unique<base::x11::xcb::tree>(x11_data.connection, x11_data.root_window);
    if (xcbtree->is_null()) {
        return;
    }

    // this constructs a vector of references with the start and end
    // of the xcbtree C pointer array of type xcb_window_t, we use reference_wrapper to only
    // create an vector of references instead of making a copy of each element into the vector.
    std::vector<std::reference_wrapper<xcb_window_t>> windows(
        xcbtree->children(), xcbtree->children() + xcbtree->data()->children_len);
    auto const& unmanaged_list = get_unmanageds(space);

    for (auto const& win : windows) {
        auto unmanaged
            = std::find_if(unmanaged_list.cbegin(), unmanaged_list.cend(), [&win](auto u) {
                  return std::visit(overload{[&](typename Space::x11_window* u) {
                                                 return win.get() == u->xcb_windows.client;
                                             },
                                             [](auto&&) { return false; }},
                                    u);
              });

        if (unmanaged != std::cend(unmanaged_list)) {
            space.stacking.order.render_overlays.push_back(*unmanaged);
        }
    }
}

template<typename Space>
void propagate_clients(Space& space, bool propagate_new_clients)
{
    using x11_window_t = typename Space::x11_window;

    if (!space.root_info) {
        return;
    }

    auto& order = space.stacking.order;

    // restack the windows according to the stacking order
    // supportWindow > electric borders > clients > hidden clients
    std::vector<xcb_window_t> stack;

    // Stack all windows under the support window. The support window is
    // not used for anything (besides the NETWM property), and it's not shown,
    // but it was lowered after kwin startup. Stacking all clients below
    // it ensures that no client will be ever shown above override-redirect
    // windows (e.g. popups).
    stack.push_back(space.root_info->supportWindow());

    auto const edges_wins = screen_edges_windows(*space.edges);
    stack.insert(stack.end(), edges_wins.begin(), edges_wins.end());
    stack.insert(stack.end(), order.manual_overlays.begin(), order.manual_overlays.end());

    // Twice the stacking-order size for inputWindow
    stack.reserve(stack.size() + 2 * order.stack.size());

    // TODO use ranges::view and ranges::transform in c++20
    std::vector<xcb_window_t> hidden_windows;
    std::for_each(order.stack.rbegin(), order.stack.rend(), [&stack, &hidden_windows](auto win) {
        std::visit(overload{[&](x11_window_t* win) {
                                // Hidden windows with preview are windows that should be unmapped
                                // but is kept for compositing ensure they are stacked below
                                // everything else (as far as pure X stacking order is concerned).
                                if (win->mapping == mapping_state::kept) {
                                    hidden_windows.push_back(win->frameId());
                                    return;
                                }

                                // Stack the input window above the frame
                                if (win->xcb_windows.input) {
                                    stack.push_back(win->xcb_windows.input);
                                }

                                stack.push_back(win->frameId());
                            },
                            [](auto&&) {}},
                   win);
    });

    // when having hidden previews, stack hidden windows below everything else
    // (as far as pure X stacking order is concerned), in order to avoid having
    // these windows that should be unmapped to interfere with other windows.
    std::copy(hidden_windows.begin(), hidden_windows.end(), std::back_inserter(stack));

    // TODO isn't it too inefficient to restack always all clients?
    // TODO don't restack not visible windows?
    Q_ASSERT(stack.at(0) == space.root_info->supportWindow());
    base::x11::xcb::restack_windows(space.base.x11_data.connection, stack);

    if (propagate_new_clients) {
        // TODO this is still not completely in the map order
        // TODO use ranges::view and ranges::transform in c++20
        std::vector<xcb_window_t> clients;
        std::vector<xcb_window_t> non_desktops;
        std::copy(order.manual_overlays.begin(),
                  order.manual_overlays.end(),
                  std::back_inserter(clients));

        for (auto const& win : space.windows) {
            std::visit(overload{[&](x11_window_t* win) {
                                    if (!win->control) {
                                        return;
                                    }

                                    if (is_desktop(win)) {
                                        clients.push_back(win->xcb_windows.client);
                                    } else {
                                        non_desktops.push_back(win->xcb_windows.client);
                                    }
                                },
                                [](auto&&) {}},
                       win);
        }

        /// Desktop windows are always on the bottom, so copy the non-desktop windows to the
        /// end/top.
        std::copy(non_desktops.begin(), non_desktops.end(), std::back_inserter(clients));
        space.root_info->setClientList(clients.data(), clients.size());
    }

    std::vector<xcb_window_t> stacked_clients;

    for (auto win : order.stack) {
        std::visit(
            overload{[&](x11_window_t* win) { stacked_clients.push_back(win->xcb_windows.client); },
                     [](auto&&) {}},
            win);
    }

    std::copy(order.manual_overlays.begin(),
              order.manual_overlays.end(),
              std::back_inserter(stacked_clients));
    space.root_info->setClientListStacking(stacked_clients.data(), stacked_clients.size());
}

template<typename Space, typename Win>
void lower_client_within_application(Space& space, Win* window)
{
    using var_win = typename Space::window_t;

    if (!window) {
        return;
    }

    window->control->cancel_auto_raise();

    blocker block(space.stacking.order);

    remove_all(space.stacking.order.pre_stack, var_win(window));

    bool lowered = false;
    // first try to put it below the bottom-most window of the application
    for (auto it = space.stacking.order.pre_stack.begin();
         it != space.stacking.order.pre_stack.end();
         ++it) {
        if (std::visit(overload{[&](auto&& win) {
                           if (win::belong_to_same_client(win, window)) {
                               space.stacking.order.pre_stack.insert(it, window);
                               lowered = true;
                               return true;
                           }
                           return false;
                       }},
                       *it)) {
            break;
        }
    }
    if (!lowered)
        space.stacking.order.pre_stack.push_front(window);
    // ignore mainwindows
}

template<typename Space, typename Win>
void raise_client_within_application(Space& space, Win* window)
{
    using var_win = typename Space::window_t;

    if (!window) {
        return;
    }

    window->control->cancel_auto_raise();

    blocker block(space.stacking.order);
    // ignore mainwindows

    // first try to put it above the top-most window of the application
    for (int i = space.stacking.order.pre_stack.size() - 1; i > -1; --i) {
        if (std::visit(overload{[&](Win* win) {
                                    if (win == window) {
                                        // Don't lower it just because it asked to be raised.
                                        return true;
                                    }
                                    if (belong_to_same_client(win, window)) {
                                        remove_all(space.stacking.order.pre_stack, var_win(window));
                                        auto it
                                            = find(space.stacking.order.pre_stack, var_win(win));
                                        assert(it != space.stacking.order.pre_stack.end());
                                        // Insert after the found one.
                                        space.stacking.order.pre_stack.insert(it + 1, window);
                                        return true;
                                    }
                                    return false;
                                },
                                [](auto&&) { return false; }},
                       space.stacking.order.pre_stack.at(i))) {
            break;
        }
    }
}

template<typename Space, typename Win>
void raise_client_request(Space& space,
                          Win* c,
                          net::RequestSource src = net::FromApplication,
                          xcb_timestamp_t timestamp = 0)
{
    if (src == net::FromTool || allow_full_window_raising(space, c, timestamp)) {
        raise_window(space, c);
    } else {
        raise_client_within_application(space, c);
        set_demands_attention(c, true);
    }
}

template<typename Space, typename Win>
void lower_client_request(Space& space,
                          Win* c,
                          net::RequestSource src,
                          [[maybe_unused]] xcb_timestamp_t /*timestamp*/)
{
    // If the client has support for all this focus stealing prevention stuff,
    // do only lowering within the application, as that's the more logical
    // variant of lowering when application requests it.
    // No demanding of attention here of course.
    if (src == net::FromTool || !has_user_time_support(c)) {
        lower_window(space, c);
    } else {
        lower_client_within_application(space, c);
    }
}

template<typename Win>
void restack_window(Win* win,
                    xcb_window_t above,
                    int detail,
                    net::RequestSource src,
                    xcb_timestamp_t timestamp,
                    bool send_event = false)
{
    using x11_window = typename Win::space_t::x11_window;
    using var_win = typename Win::space_t::window_t;

    Win* other = nullptr;
    if (detail == XCB_STACK_MODE_OPPOSITE) {
        other = find_controlled_window<x11_window>(win->space, predicate_match::window, above);
        if (!other) {
            raise_or_lower_client(win->space, win);
            return;
        }

        auto it = win->space.stacking.order.stack.cbegin();
        auto end = win->space.stacking.order.stack.cend();

        while (it != end) {
            if (*it == var_win(win)) {
                detail = XCB_STACK_MODE_ABOVE;
                break;
            } else if (*it == var_win(other)) {
                detail = XCB_STACK_MODE_BELOW;
                break;
            }
            ++it;
        }
    } else if (detail == XCB_STACK_MODE_TOP_IF) {
        other = find_controlled_window<x11_window>(win->space, predicate_match::window, above);
        if (other && other->geo.frame.intersects(win->geo.frame)) {
            raise_client_request(win->space, win, src, timestamp);
        }
        return;
    } else if (detail == XCB_STACK_MODE_BOTTOM_IF) {
        other = find_controlled_window<x11_window>(win->space, predicate_match::window, above);
        if (other && other->geo.frame.intersects(win->geo.frame)) {
            lower_client_request(win->space, win, src, timestamp);
        }
        return;
    }

    if (!other) {
        other = find_controlled_window<x11_window>(win->space, predicate_match::window, above);
    }

    if (other && detail == XCB_STACK_MODE_ABOVE) {
        auto it = win->space.stacking.order.stack.cend();
        auto begin = win->space.stacking.order.stack.cbegin();

        while (--it != begin) {
            if (*it == var_win(other)) {
                // the other one is top on stack
                // invalidate and force
                it = begin;
                src = net::FromTool;
                break;
            }

            if (!std::holds_alternative<Win*>(*it)) {
                continue;
            }

            auto above_win = std::get<Win*>(*it);

            if (!(is_normal(above_win) && above_win->isShown() && on_current_desktop(*above_win)
                  && on_screen(above_win, win->topo.central_output))) {
                continue;
            }

            if (*(it - 1) == var_win(other)) {
                // "it" is the one above the target one, stack below "it"
                break;
            }
        }

        if (it != begin && (*(it - 1) == var_win(other))) {
            other = std::get<Win*>(*it);
        } else {
            other = nullptr;
        }
    }

    if (other) {
        restack(win->space, win, other);
    } else if (detail == XCB_STACK_MODE_BELOW) {
        lower_client_request(win->space, win, src, timestamp);
    } else if (detail == XCB_STACK_MODE_ABOVE) {
        raise_client_request(win->space, win, src, timestamp);
    }

    if (send_event) {
        send_synthetic_configure_notify(win, frame_to_client_rect(win, win->geo.frame));
    }
}

}
