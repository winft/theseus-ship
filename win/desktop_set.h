/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "desktop_get.h"
#include "focus_chain_edit.h"
#include "stacking.h"
#include "transient.h"

#include "main.h"

namespace KWin::win
{

template<typename Win>
void propagate_on_all_desktops_to_children(Win& window);

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
    win->control->set_desktops(desktops);

    if ((was_desk == NET::OnAllDesktops) != (win->desktop() == NET::OnAllDesktops)) {
        // OnAllDesktops changed
        propagate_on_all_desktops_to_children(*win);
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
    win->updateWindowRules(rules::type::desktop);

    Q_EMIT win->qobject->desktopChanged();
    if (wasOnCurrentDesktop != on_current_desktop(win)) {
        Q_EMIT win->qobject->desktopPresenceChanged(was_desk);
    }
    Q_EMIT win->qobject->x11DesktopIdsChanged();
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
    desktop = std::min(desktops_count, win->control->rules.checkDesktop(desktop));

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

template<typename Win>
void propagate_on_all_desktops_to_children(Win& window)
{
    for (auto const& transient : window.transient()->children) {
        if (transient->isOnAllDesktops() != window.isOnAllDesktops()) {
            set_on_all_desktops(transient, window.isOnAllDesktops());
        }
    }
}

}
