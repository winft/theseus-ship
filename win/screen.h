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
#include "screens.h"

#include <Wrapland/Server/plasma_window.h>

namespace KWin::win
{

template<typename Win>
bool on_screen(Win* win, int screen)
{
    return kwinApp()->get_base().screens.geometry(screen).intersects(win->frameGeometry());
}

template<typename Space>
int get_current_output(Space const& space)
{
    auto const& base = kwinApp()->get_base();

    if (kwinApp()->options->get_current_output_follows_mouse()) {
        return base::get_nearest_output(base.get_outputs(), input::get_cursor()->pos());
    }

    auto const cur = base.topology.current;
    if (auto client = space.activeClient(); client && !win::on_screen(client, cur)) {
        return base::get_output_index(kwinApp()->get_base().get_outputs(), client->central_output);
    }
    return cur;
}

template<typename Base, typename Win>
void set_current_output_by_window(Base& base, Win const& window)
{
    if (!window.control->active()) {
        return;
    }
    if (!win::on_screen(&window, base.topology.current)) {
        base::set_current_output(base,
                                 base::get_output_index(base.get_outputs(), window.central_output));
    }
}

template<typename Win>
bool on_active_screen(Win* win)
{
    return on_screen(win, get_current_output(*workspace()));
}

template<typename Win>
void send_to_screen(Win* win, int new_screen)
{
    new_screen = win->control->rules().checkScreen(new_screen);
    auto new_output = base::get_output(kwinApp()->get_base().get_outputs(), new_screen);

    if (win->control->active()) {
        set_current_output(kwinApp()->get_base(), new_screen);
        // might impact the layer of a fullscreen window
        for (auto cc : workspace()->allClientList()) {
            if (cc->control->fullscreen() && cc->central_output == new_output) {
                update_layer(cc);
            }
        }
    }

    if (win->central_output == new_output) {
        // Don't use isOnScreen(), that's true even when only partially.
        return;
    }

    geometry_updates_blocker blocker(win);

    // operating on the maximized / quicktiled window would leave the old geom_restore behind,
    // so we clear the state first
    auto const old_restore_geo = win->restore_geometries.maximize;
    auto const old_frame_geo = win->geometry_update.frame;
    auto frame_geo = old_restore_geo.isValid() ? old_restore_geo : old_frame_geo;

    auto max_mode = win->geometry_update.max_mode;
    auto qtMode = win->control->quicktiling();
    if (max_mode != maximize_mode::restore) {
        maximize(win, win::maximize_mode::restore);
    }

    if (qtMode != quicktiles::none) {
        set_quicktile_mode(win, quicktiles::none, true);
    }

    auto oldScreenArea = workspace()->clientArea(MaximizeArea, win);
    auto screenArea = workspace()->clientArea(MaximizeArea, new_screen, win->desktop());

    // the window can have its center so that the position correction moves the new center onto
    // the old screen, what will tile it where it is. Ie. the screen is not changed
    // this happens esp. with electric border quicktiling
    if (qtMode != quicktiles::none) {
        keep_in_area(win, oldScreenArea, false);
    }

    // Move the window to have the same relative position to the center of the screen
    // (i.e. one near the middle of the right edge will also end up near the middle of the right
    // edge).
    auto center = frame_geo.center() - oldScreenArea.center();
    center.setX(center.x() * screenArea.width() / oldScreenArea.width());
    center.setY(center.y() * screenArea.height() / oldScreenArea.height());
    center += screenArea.center();
    frame_geo.moveCenter(center);

    win->setFrameGeometry(frame_geo);

    // If the window was inside the old screen area, explicitly make sure its inside also the new
    // screen area. Calling checkWorkspacePosition() should ensure that, but when moving to a small
    // screen the window could be big enough to overlap outside of the new screen area, making
    // struts from other screens come into effect, which could alter the resulting geometry.
    if (oldScreenArea.contains(old_frame_geo)) {
        keep_in_area(win, screenArea, false);
    }

    // The call to check_workspace_position(..) does change up the geometry-update again, making it
    // possibly the size of the whole screen. Therefore rememeber the current geometry for if
    // required setting later the restore geometry here.
    auto const restore_geo = win->geometry_update.frame;

    check_workspace_position(win, old_frame_geo);

    // finally reset special states
    // NOTICE that MaximizeRestore/quicktiles::none checks are required.
    // eg. setting quicktiles::none would break maximization
    if (max_mode != maximize_mode::restore) {
        maximize(win, max_mode);
        win->restore_geometries.maximize = restore_geo;
    }

    if (qtMode != quicktiles::none && qtMode != win->control->quicktiling()) {
        set_quicktile_mode(win, qtMode, true);
        win->restore_geometries.maximize = restore_geo;
    }

    auto children = restacked_by_space_stacking_order(workspace(), win->transient()->children);
    for (auto const& child : children) {
        if (child->control) {
            send_to_screen(child, new_screen);
        }
    }
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
                ? win->desktops().contains(virtual_desktop_manager::self()->desktopForX11Id(d))
                : win->desktop() == d)
        || on_all_desktops(win);
}

template<typename Win>
bool on_current_desktop(Win* win)
{
    return on_desktop(win, virtual_desktop_manager::self()->current());
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
        workspace()->updateOnAllDesktopsOfTransients(win);
    }

    auto transients_stacking_order
        = restacked_by_space_stacking_order(workspace(), win->transient()->children);
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

    focus_chain::self()->update(win, focus_chain::MakeFirst);
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
    auto const desktops_count = static_cast<int>(virtual_desktop_manager::self()->count());
    if (desktop != NET::OnAllDesktops) {
        // Check range.
        desktop = std::max(1, std::min(desktops_count, desktop));
    }
    desktop = std::min(desktops_count, win->control->rules().checkDesktop(desktop));

    QVector<virtual_desktop*> desktops;
    if (desktop != NET::OnAllDesktops) {
        desktops << virtual_desktop_manager::self()->desktopForX11Id(desktop);
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
        set_desktop(win, virtual_desktop_manager::self()->current());
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
        currentDesktops = virtual_desktop_manager::self()->desktops();
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
