/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_WIN_SCREEN_H
#define KWIN_WIN_SCREEN_H

#include "net.h"
#include "types.h"

#include "main.h"
#include "screens.h"
#include "virtualdesktops.h"

namespace KWin::win
{

template<typename Win>
bool on_screen(Win* win, int screen)
{
    return screens()->geometry(screen).intersects(win->frameGeometry());
}

template<typename Win>
bool on_active_screen(Win* win)
{
    return on_screen(win, screens()->current());
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
                ? win->desktops().contains(VirtualDesktopManager::self()->desktopForX11Id(d))
                : win->desktop() == d)
        || on_all_desktops(win);
}

template<typename Win>
bool on_current_desktop(Win* win)
{
    return on_desktop(win, VirtualDesktopManager::self()->current());
}

/**
 * Deprecated, use x11_desktop_ids.
 */
template<typename Win>
void set_desktop(Win* win, int desktop)
{
    auto const desktops_count = static_cast<int>(VirtualDesktopManager::self()->count());
    if (desktop != NET::OnAllDesktops) {
        // Check range.
        desktop = std::max(1, std::min(desktops_count, desktop));
    }
    desktop = std::min(desktops_count, win->rules()->checkDesktop(desktop));

    QVector<VirtualDesktop*> desktops;
    if (desktop != NET::OnAllDesktops) {
        desktops << VirtualDesktopManager::self()->desktopForX11Id(desktop);
    }
    win->setDesktops(desktops);
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
        set_desktop(win, VirtualDesktopManager::self()->current());
    }
}

template<typename Win>
QVector<uint> x11_desktop_ids(Win* win)
{
    auto const desks = win->desktops();
    QVector<uint> x11_ids;
    x11_ids.reserve(desks.count());
    std::transform(desks.constBegin(),
                   desks.constEnd(),
                   std::back_inserter(x11_ids),
                   [](VirtualDesktop const* vd) { return vd->x11DesktopNumber(); });
    return x11_ids;
}

template<typename Win>
void enter_desktop(Win* win, VirtualDesktop* virtualDesktop)
{
    if (win->desktops().contains(virtualDesktop)) {
        return;
    }
    auto desktops = win->desktops();
    desktops.append(virtualDesktop);
    win->setDesktops(desktops);
}

template<typename Win>
void leave_desktop(Win* win, VirtualDesktop* virtualDesktop)
{
    QVector<VirtualDesktop*> currentDesktops;
    if (win->desktops().isEmpty()) {
        currentDesktops = VirtualDesktopManager::self()->desktops();
    } else {
        currentDesktops = win->desktops();
    }

    if (!currentDesktops.contains(virtualDesktop)) {
        return;
    }
    auto desktops = currentDesktops;
    desktops.removeOne(virtualDesktop);
    win->setDesktops(desktops);
}

template<typename Win>
bool on_all_activities(Win* win)
{
    return win->activities().isEmpty();
}

template<typename Win>
bool on_activity(Win* win, QString const& activity)
{
    return on_all_activities(win) || win->activities().contains(activity);
}

}

#endif
