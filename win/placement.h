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
#include "types.h"
#include <kwinglobals.h>
// Qt
#include <QList>
#include <QPoint>
#include <QRect>

namespace KWin
{
class Toplevel;

namespace win
{
KWIN_EXPORT void place(Toplevel* window, const QRect& area);

KWIN_EXPORT void
place_at_random(Toplevel* window, const QRect& area, placement next = placement::unknown);
KWIN_EXPORT void
place_smart(Toplevel* window, const QRect& area, placement next = placement::unknown);
KWIN_EXPORT void
place_maximizing(Toplevel* window, const QRect& area, placement next = placement::unknown);
KWIN_EXPORT void
place_centered(Toplevel* window, const QRect& area, placement next = placement::unknown);
KWIN_EXPORT void
place_zero_cornered(Toplevel* window, const QRect& area, placement next = placement::unknown);
KWIN_EXPORT void
place_dialog(Toplevel* window, const QRect& area, placement next = placement::unknown);
KWIN_EXPORT void
place_utility(Toplevel* window, const QRect& area, placement next = placement::unknown);
KWIN_EXPORT void place_on_screen_display(Toplevel* window, const QRect& area);

/**
 * Unclutters the current desktop by smart-placing all clients again.
 */
KWIN_EXPORT void unclutter_desktop();

KWIN_EXPORT bool is_irrelevant(Toplevel const* window, Toplevel const* regarding, int desktop);
KWIN_EXPORT bool can_move(Toplevel const* window);

KWIN_EXPORT void place(Toplevel* window,
                       const QRect& area,
                       placement policy,
                       placement nextPlacement = placement::unknown);
KWIN_EXPORT void
place_under_mouse(Toplevel* window, const QRect& area, placement next = placement::unknown);
KWIN_EXPORT void
place_on_main_window(Toplevel* window, const QRect& area, placement next = placement::unknown);

}

} // namespace

#endif
