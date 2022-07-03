/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "focus_chain.h"
#include "move.h"
#include "net.h"
#include "stacking.h"
#include "transient.h"
#include "types.h"
#include "virtual_desktops.h"

#include "base/output_helpers.h"
#include "main.h"

#include <Wrapland/Server/plasma_window.h>

namespace KWin::win
{

template<typename Win>
bool on_screen(Win* win, base::output const* output)
{
    if (!output) {
        return false;
    }
    return output->geometry().intersects(win->frameGeometry());
}

/**
 * @brief Finds the best window to become the new active window in the focus chain for the given
 * virtual @p desktop.
 *
 * In case that separate output focus is used only windows on the current output are considered.
 * If no window for activation is found @c null is returned.
 *
 * @param desktop The virtual desktop to look for a window for activation
 * @return The window which could be activated or @c null if there is none.
 */
template<typename Space>
base::output const* get_current_output(Space const& space)
{
    auto const& base = kwinApp()->get_base();

    if (kwinApp()->options->get_current_output_follows_mouse()) {
        return base::get_nearest_output(base.get_outputs(), input::get_cursor()->pos());
    }

    auto const cur = base.topology.current;
    if (auto client = space.active_client; client && !win::on_screen(client, cur)) {
        return client->central_output;
    }
    return cur;
}

/**
 * @brief Finds the best window to become the new active window in the focus chain for the given
 * virtual @p desktop on the given @p output.
 *
 * This method makes only sense to use if separate output focus is used. If separate output
 * focus is disabled the @p output is ignored. If no window for activation is found @c null is
 * returned.
 *
 * @param desktop The virtual desktop to look for a window for activation
 * @param output The output to constrain the search on with separate output focus
 * @return The window which could be activated or @c null if there is none.
 */
template<typename Win, typename Manager>
Win* focus_chain_get_for_activation(Manager& manager, uint desktop, base::output const* output)
{
    auto desk_it = manager.chains.desktops.constFind(desktop);
    if (desk_it == manager.chains.desktops.constEnd()) {
        return nullptr;
    }

    auto const& chain = desk_it.value();

    // TODO(romangg): reverse-range with C++20
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        // TODO: move the check into Client
        auto win = *it;
        if (!win->isShown()) {
            continue;
        }
        if (manager.has_separate_screen_focus && win->central_output != output) {
            continue;
        }
        return win;
    }

    return nullptr;
}

template<typename Win, typename Manager>
Win* focus_chain_get_for_activation_on_current_output(Manager& manager, uint desktop)
{
    return focus_chain_get_for_activation<Win>(manager, desktop, get_current_output(manager.space));
}

template<typename Manager>
bool focus_chain_is_usable_focus_candidate(Manager& manager, Toplevel* window, Toplevel* prev)
{
    if (window == prev) {
        return false;
    }
    if (!window->isShown() || !window->isOnCurrentDesktop()) {
        return false;
    }

    if (!manager.has_separate_screen_focus) {
        return true;
    }

    return on_screen(window, prev ? prev->central_output : get_current_output(manager.space));
}

/**
 * @brief Queries the focus chain for @p desktop for the next window in relation to the given
 * @p reference.
 *
 * The method finds the first usable window which is not the @p reference Client. If no Client
 * can be found @c null is returned
 *
 * @param reference The reference window which should not be returned
 * @param desktop The virtual desktop whose focus chain should be used
 * @return *The next usable window or @c null if none can be found.
 */
template<typename Manager, typename Win>
Toplevel* focus_chain_next_for_desktop(Manager& manager, Win* reference, uint desktop)
{
    auto desk_it = manager.chains.desktops.constFind(desktop);
    if (desk_it == manager.chains.desktops.constEnd()) {
        return nullptr;
    }

    auto const& chain = desk_it.value();

    // TODO(romangg): reverse-range with C++20
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (focus_chain_is_usable_focus_candidate(manager, *it, reference)) {
            return *it;
        }
    }

    return nullptr;
}

template<typename Base, typename Win>
void set_current_output_by_window(Base& base, Win const& window)
{
    if (!window.control->active()) {
        return;
    }
    if (window.central_output && !win::on_screen(&window, base.topology.current)) {
        base::set_current_output(base, window.central_output);
    }
}

template<typename Win>
bool on_active_screen(Win* win)
{
    return on_screen(win, get_current_output(win->space));
}

template<typename Win>
bool on_all_desktops(Win* win)
{
    return kwinApp()->operationMode() == Application::OperationModeWaylandOnly
            || kwinApp()->operationMode() == Application::OperationModeXwayland
        // Wayland
        ? win->desktops().isEmpty()
        // X11
        : win->desktop() == NET::OnAllDesktops;
}

template<typename Win>
bool on_desktop(Win* win, int d)
{
    return (kwinApp()->operationMode() == Application::OperationModeWaylandOnly
                    || kwinApp()->operationMode() == Application::OperationModeXwayland
                ? win->desktops().contains(win->space.virtual_desktop_manager->desktopForX11Id(d))
                : win->desktop() == d)
        || on_all_desktops(win);
}

template<typename Win>
bool on_current_desktop(Win* win)
{
    return on_desktop(win, win->space.virtual_desktop_manager->current());
}

template<typename Win>
void set_desktops(Win* win, QVector<virtual_desktop*> desktops)
{
    // On x11 we can have only one desktop at a time.
    if (kwinApp()->operationMode() == Application::OperationModeX11 && desktops.size() > 1) {
        desktops = QVector<virtual_desktop*>({desktops.last()});
    }

    if (desktops == win->desktops()) {
        return;
    }

    auto was_desk = win->desktop();
    auto const wasOnCurrentDesktop = on_current_desktop(win) && was_desk >= 0;

    win->set_desktops(desktops);

    if (auto management = win->control->wayland_management()) {
        if (desktops.isEmpty()) {
            management->setOnAllDesktops(true);
        } else {
            management->setOnAllDesktops(false);
            auto currentDesktops = management->plasmaVirtualDesktops();
            for (auto desktop : desktops) {
                auto id = desktop->id().toStdString();
                if (!contains(currentDesktops, id)) {
                    management->addPlasmaVirtualDesktop(id);
                } else {
                    remove_all(currentDesktops, id);
                }
            }
            for (auto desktop : currentDesktops) {
                management->removePlasmaVirtualDesktop(desktop);
            }
        }
    }
    if (win->info) {
        win->info->setDesktop(win->desktop());
    }

    if ((was_desk == NET::OnAllDesktops) != (win->desktop() == NET::OnAllDesktops)) {
        // OnAllDesktops changed
        win->space.updateOnAllDesktopsOfTransients(win);
    }

    auto transients_stacking_order
        = restacked_by_space_stacking_order(&win->space, win->transient()->children);
    for (auto const& child : transients_stacking_order) {
        if (!child->transient()->annexed) {
            set_desktops(child, desktops);
        }
    }

    if (win->transient()->modal()) {
        // When a modal dialog is moved move the parent window with it as otherwise the just moved
        // modal dialog will return to the parent window with the next desktop change.
        for (auto client : win->transient()->leads()) {
            set_desktops(client, desktops);
        }
    }

    win->doSetDesktop(win->desktop(), was_desk);

    focus_chain_update(win->space.focus_chain, win, focus_chain_change::make_first);
    win->updateWindowRules(Rules::Desktop);

    Q_EMIT win->desktopChanged();
    if (wasOnCurrentDesktop != on_current_desktop(win)) {
        Q_EMIT win->desktopPresenceChanged(win, was_desk);
    }
    Q_EMIT win->x11DesktopIdsChanged();
}

/**
 * Deprecated, use x11_desktop_ids.
 */
template<typename Win>
void set_desktop(Win* win, int desktop)
{
    auto const desktops_count = static_cast<int>(win->space.virtual_desktop_manager->count());
    if (desktop != NET::OnAllDesktops) {
        // Check range.
        desktop = std::max(1, std::min(desktops_count, desktop));
    }
    desktop = std::min(desktops_count, win->control->rules().checkDesktop(desktop));

    QVector<virtual_desktop*> desktops;
    if (desktop != NET::OnAllDesktops) {
        desktops << win->space.virtual_desktop_manager->desktopForX11Id(desktop);
    }
    set_desktops(win, desktops);
}

template<typename Win>
void set_on_all_desktops(Win* win, bool set)
{
    if (set == on_all_desktops(win)) {
        return;
    }

    if (set) {
        set_desktop(win, NET::OnAllDesktops);
    } else {
        set_desktop(win, win->space.virtual_desktop_manager->current());
    }
}

template<typename Win>
QVector<uint> x11_desktop_ids(Win* win)
{
    auto const desks = win->desktops();
    QVector<uint> x11_ids;
    x11_ids.reserve(desks.count());
    std::transform(desks.constBegin(), desks.constEnd(), std::back_inserter(x11_ids), [](auto vd) {
        return vd->x11DesktopNumber();
    });
    return x11_ids;
}

template<typename Win>
void enter_desktop(Win* win, virtual_desktop* virtualDesktop)
{
    if (win->desktops().contains(virtualDesktop)) {
        return;
    }
    auto desktops = win->desktops();
    desktops.append(virtualDesktop);
    set_desktops(win, desktops);
}

template<typename Win>
void leave_desktop(Win* win, virtual_desktop* virtualDesktop)
{
    QVector<virtual_desktop*> currentDesktops;
    if (win->desktops().isEmpty()) {
        currentDesktops = win->space.virtual_desktop_manager->desktops();
    } else {
        currentDesktops = win->desktops();
    }

    if (!currentDesktops.contains(virtualDesktop)) {
        return;
    }
    auto desktops = currentDesktops;
    desktops.removeOne(virtualDesktop);
    set_desktops(win, desktops);
}

}
