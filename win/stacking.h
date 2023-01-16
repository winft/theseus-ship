/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "controlling.h"
#include "focus_chain.h"
#include "focus_chain_edit.h"
#include "geo.h"
#include "layers.h"
#include "meta.h"
#include "util.h"

#include "base/output_helpers.h"
#include "base/platform.h"
#include "main.h"
#include "utils/algorithm.h"
#include "utils/blocker.h"

#include "rules/ruling.h"

#include <deque>
#include <map>

/**
 This file contains things relevant to stacking order and layers.

 Design:

 Normal unconstrained stacking order, as requested by the user (by clicking
 on windows to raise them, etc.), is in Workspace::unconstrained_stacking_order.
 That list shouldn't be used at all, except for building
 Workspace::stacking_order. The building is done
 in Workspace::constrainedStackingOrder(). Only Workspace::stackingOrder() should
 be used to get the stacking order, because it also checks the stacking order
 is up to date.
 All clients are also stored in Workspace::clients (except for isDesktop() clients,
 as those are very special, and are stored in Workspace::desktops), in the order
 the clients were created.

 Every window has one layer assigned in which it is. There are 7 layers,
 from bottom : DesktopLayer, BelowLayer, NormalLayer, DockLayer, AboveLayer, NotificationLayer,
 ActiveLayer, CriticalNotificationLayer, and OnScreenDisplayLayer (see also NETWM sect.7.10.).
 The layer a window is in depends on the window type, and on other things like whether the window
 is active. We extend the layers provided in NETWM by the NotificationLayer, OnScreenDisplayLayer,
 and CriticalNotificationLayer.
 The NoficationLayer contains notification windows which are kept above all windows except the
 active fullscreen window. The CriticalNotificationLayer contains notification windows which are
 important enough to keep them even above fullscreen windows. The OnScreenDisplayLayer is used for
 eg. volume and brightness change feedback and is kept above all windows since it provides immediate
 response to a user action.

 NET::Splash clients belong to the Normal layer. NET::TopMenu clients
 belong to Dock layer. Clients that are both NET::Dock and NET::KeepBelow
 are in the Normal layer in order to keep the 'allow window to cover
 the panel' Kicker setting to work as intended (this may look like a slight
 spec violation, but a) I have no better idea, b) the spec allows adjusting
 the stacking order if the WM thinks it's a good idea . We put all
 NET::KeepAbove above all Docks too, even though the spec suggests putting
 them in the same layer.

 Most transients are in the same layer as their mainwindow,
 see Workspace::constrainedStackingOrder(), they may also be in higher layers, but
 they should never be below their mainwindow.

 When some client attribute changes (above/below flag, transiency...),
 win::update_layer() should be called in order to make
 sure it's moved to the appropriate layer QList<X11Client *> if needed.

 Currently the things that affect client in which layer a client
 belongs: KeepAbove/Keep Below flags, window type, fullscreen
 state and whether the client is active, mainclient (transiency).

 Make sure updateStackingOrder() is called in order to make
 Workspace::stackingOrder() up to date and propagated to the world.
 Using Workspace::blockStackingUpdates() (or the StackingUpdatesBlocker
 helper class) it's possible to temporarily disable updates
 and the stacking order will be updated once after it's allowed again.
*/

namespace KWin::win
{

/**
 * Returns topmost visible client. Windows on the dock, the desktop
 * or of any other special kind are excluded. Also if the window
 * doesn't accept focus it's excluded.
 */
// TODO misleading name for this method, too many slightly different ways to use it
template<typename Space>
std::optional<typename Space::window_t> top_client_on_desktop(Space& space,
                                                              int desktop,
                                                              base::output const* output,
                                                              bool unconstrained = false,
                                                              bool only_normal = true)
{
    auto const& list = unconstrained ? space.stacking.order.pre_stack : space.stacking.order.stack;
    for (auto it = std::crbegin(list); it != std::crend(list); it++) {
        if (std::visit(overload{[&](auto&& win) {
                           if (!on_desktop(win, desktop)) {
                               return false;
                           }
                           if (!win->isShown()) {
                               return false;
                           }
                           if (output && win->topo.central_output != output) {
                               return false;
                           }
                           if (!only_normal) {
                               return true;
                           }
                           if (wants_tab_focus(win) && !is_special_window(win)) {
                               return true;
                           }
                           return false;
                       }},
                       *it)) {
            return *it;
        }
    }
    return {};
}

template<class Order, class T, class R = T>
std::deque<T*> ensure_stacking_order_in_list(Order const& order, std::vector<T*> const& list)
{
    if (list.empty()) {
        return {};
    }
    if (list.size() < 2) {
        return {list.front()};
    }

    // TODO is this worth optimizing?
    std::deque<T*> result;
    std::copy(begin(list), end(list), back_inserter(result));

    // Now reorder the result. For that 'order' should be a superset and it define the order in
    // which windows should appear in result. We then reorder result simply by going through order
    // one-by-one, removing it from result and then adding it back in the end.
    for (auto const& win : order.stack) {
        if (std::holds_alternative<T*>(win)) {
            move_to_back(result, std::get<T*>(win));
        }
    }

    return result;
}

template<class Space, class Win>
std::deque<Win*> restacked_by_space_stacking_order(Space& space, std::vector<Win*> const& list)
{
    return ensure_stacking_order_in_list(space.stacking.order, list);
}

template<typename Space, typename Window>
void lower_window(Space& space, Window* window)
{
    using var_win = typename Space::window_t;

    assert(window->control);

    auto do_lower = [&space](auto win) {
        win->control->cancel_auto_raise();

        blocker block(space.stacking.order);

        auto& pre_stack = space.stacking.order.pre_stack;
        if (!move_to_front(pre_stack, var_win(win))) {
            pre_stack.push_front(win);
        }

        return block;
    };
    auto cleanup = [&space](auto win) {
        if (var_win(win) == space.stacking.most_recently_raised) {
            space.stacking.most_recently_raised = {};
        }
    };

    auto block = do_lower(window);

    // TODO(romangg): Factor this out in separatge function.
    if constexpr (std::is_same_v<typename Space::x11_window, Window>) {
        if (window->transient->lead() && window->group) {
            // Lower also all windows in the group, in reversed stacking order.
            auto const wins
                = restacked_by_space_stacking_order(space, get_transient_family(window));

            for (auto it = wins.crbegin(); it != wins.crend(); it++) {
                auto gwin = *it;
                if (gwin == window) {
                    continue;
                }

                assert(gwin->control);
                do_lower(gwin);
                cleanup(gwin);
            }
        }
    }

    cleanup(window);
}

template<typename Space, typename Window>
void raise_window(Space& space, Window* window)
{
    using var_win = typename Space::window_t;

    if (!window) {
        return;
    }

    auto prepare = [&space](auto window) {
        assert(window->control);
        window->control->cancel_auto_raise();
        return blocker(space.stacking.order);
    };
    auto do_raise = [&space](auto window) {
        if (!move_to_back(space.stacking.order.pre_stack, var_win(window))) {
            // Window not yet in pre-stack. Can happen on creation. It will be raised once shown.
            return;
        }

        if (!is_special_window(window)) {
            space.stacking.most_recently_raised = window;
        }
    };

    auto block = prepare(window);

    if (window->transient->lead()) {
        // Also raise all leads.
        std::vector<Window*> leads;

        for (auto lead : window->transient->leads()) {
            while (lead) {
                if (!contains(leads, lead)) {
                    leads.push_back(lead);
                }
                lead = lead->transient->lead();
            }
        }

        for (auto lead : restacked_by_space_stacking_order(space, leads)) {
            if (!lead->control) {
                // Might be without control, at least on X11 this can happen (latte-dock settings).
                continue;
            }
            auto block = prepare(lead);
            do_raise(lead);
        }
    }

    do_raise(window);
}

template<typename Space, typename Window>
void raise_or_lower_client(Space& space, Window* window)
{
    using var_win = typename Space::window_t;

    if (!window) {
        return;
    }

    std::optional<var_win> topmost;

    if (space.stacking.most_recently_raised
        && contains(space.stacking.order.stack, space.stacking.most_recently_raised)
        && std::visit(overload{[](auto&& win) { return win->isShown(); }},
                      *space.stacking.most_recently_raised)
        && on_current_desktop(window)) {
        topmost = space.stacking.most_recently_raised;
    } else {
        topmost = top_client_on_desktop(
            space,
            on_all_desktops(window) ? space.virtual_desktop_manager->current()
                                    : get_desktop(*window),
            space.base.options->qobject->isSeparateScreenFocus() ? window->topo.central_output
                                                                 : nullptr);
    }

    if (topmost && var_win(window) == *topmost) {
        lower_window(space, window);
    } else {
        raise_window(space, window);
    }
}

template<typename Space, typename Win, typename UnderWin>
void restack(Space& space, Win* window, UnderWin* under, bool force = false)
{
    using var_win = typename Space::window_t;

    assert(contains(space.stacking.order.pre_stack, var_win(under)));

    if (!force && !belong_to_same_client(under, window)) {
        // put in the stacking order below _all_ windows belonging to the active application
        for (auto it = space.stacking.order.pre_stack.crbegin();
             it != space.stacking.order.pre_stack.crend();
             it++) {
            if (std::visit(overload{[&](UnderWin* other) {
                                        if (!other->control
                                            || get_layer(*other) != get_layer(*window)
                                            || !belong_to_same_client(under, other)) {
                                            return false;
                                        }

                                        // window doesn't belong to the same client as under, as we
                                        // checked above, but other does, so window can't be other.
                                        assert(var_win(window) != var_win(other));
                                        under = other;
                                        return true;
                                    },
                                    [&](auto&& /*win*/) { return false; }},
                           *it)) {
                break;
            }
        }
    }

    assert(under);

    remove_all(space.stacking.order.pre_stack, var_win(window));
    auto it = find(space.stacking.order.pre_stack, var_win(under));
    space.stacking.order.pre_stack.insert(it, var_win(window));

    focus_chain_move_window_after(space.stacking.focus_chain, window, under);
    space.stacking.order.update_order();
}

template<typename Space, typename Win>
void restack_client_under_active(Space& space, Win& win)
{
    using var_win = typename Space::window_t;

    if (!space.stacking.active || space.stacking.active == var_win(&win)) {
        raise_window(space, &win);
        return;
    }

    std::visit(overload{[&](auto&& act_win) {
                   if (get_layer(*act_win) != get_layer(win)) {
                       raise_window(space, &win);
                       return;
                   }
                   restack(space, &win, act_win);
               }},
               *space.stacking.active);
}

template<typename Win>
void auto_raise(Win& win)
{
    raise_window(win.space, &win);
    win.control->cancel_auto_raise();
}

/**
 * Group windows by layer, than flatten to a list.
 * @param list container of windows to sort
 */
template<typename Container>
std::vector<typename Container::value_type> sort_windows_by_layer(Container const& list)
{
    using order_window_t = typename Container::value_type;
    std::deque<order_window_t> layers[enum_index(layer::count)];

    // Build the order from layers.

    // This is needed as a workaround for group windows with fullscreen members, such that other
    // group members are moved per output to the active (fullscreen) level too.
    using key = std::pair<base::output const*, order_window_t>;
    std::map<key, layer> lead_layers;

    for (auto const& window : list) {
        layer lay;
        std::visit(overload{[&](auto&& win) {
                       lay = get_layer(*win);
                       auto lead = get_top_lead(win);
                       auto search = lead_layers.find({win->topo.central_output, lead});

                       if (search != lead_layers.end()) {
                           // If a window is raised above some other window in the same window group
                           // which is in the ActiveLayer (i.e. it's fulscreened), make sure it
                           // stays above that window (see #95731).
                           if (search->second == layer::active
                               && (enum_index(lay) > enum_index(layer::below))) {
                               lay = layer::active;
                           }
                           search->second = lay;
                       } else {
                           lead_layers[{win->topo.central_output, lead}] = lay;
                       }
                   }},
                   window);

        layers[enum_index(lay)].push_back(window);
    }

    std::vector<order_window_t> sorted;
    sorted.reserve(list.size());

    for (auto lay = enum_index(layer::first); lay < enum_index(layer::count); ++lay) {
        sorted.insert(sorted.end(), layers[lay].begin(), layers[lay].end());
    }

    return sorted;
}

}
