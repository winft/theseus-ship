/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 1997 to 2002 Cristian Tibirna <tibirna@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

#ifndef KWIN_PLACEMENT_H
#define KWIN_PLACEMENT_H
// KWin
#include <kwinglobals.h>
// Qt
#include <QList>
#include <QPoint>
#include <QRect>

class QObject;

namespace KWin
{
class Toplevel;

class KWIN_EXPORT Placement
{
public:
    virtual ~Placement();

    /**
     * Placement policies. How workspace decides the way windows get positioned
     * on the screen. The better the policy, the heavier the resource use.
     * Normally you don't have to worry. What the WM adds to the startup time
     * is nil compared to the creation of the window itself in the memory
     */
    enum Policy {
        no_placement,   // not really a placement
        global_default, // special, means to use the global default
        unknown,        // special, means the function should use its default
        random,
        smart,
        centered,
        zero_cornered,
        under_mouse,    // special
        on_main_window, // special
        maximizing,
    };

    void place(Toplevel* window, const QRect& area);

    void place_at_random(Toplevel* window, const QRect& area, Policy next = unknown);
    void place_smart(Toplevel* window, const QRect& area, Policy next = unknown);
    void place_maximizing(Toplevel* window, const QRect& area, Policy next = unknown);
    void place_centered(Toplevel* window, const QRect& area, Policy next = unknown);
    void place_zero_cornered(Toplevel* window, const QRect& area, Policy next = unknown);
    void place_dialog(Toplevel* window, const QRect& area, Policy next = unknown);
    void place_utility(Toplevel* window, const QRect& area, Policy next = unknown);
    void place_on_screen_display(Toplevel* window, const QRect& area);

    /**
     *   Unclutters the current desktop by smart-placing all clients again.
     */
    void unclutter_desktop();

    static const char* policy_to_string(Policy policy);

    static bool is_irrelevant(Toplevel const* window, Toplevel const* regarding, int desktop);
    static bool can_move(Toplevel const* window);

private:
    void place(Toplevel* window, const QRect& area, Policy policy, Policy nextPlacement = unknown);
    void place_under_mouse(Toplevel* window, const QRect& area, Policy next = unknown);
    void place_on_main_window(Toplevel* window, const QRect& area, Policy next = unknown);

    KWIN_SINGLETON(Placement)
};

} // namespace

#endif
