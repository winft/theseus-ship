/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2011 Arthur Arlt <a.arlt@stud.uni-heidelberg.de>

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
#ifndef KWIN_OVERLAYWINDOW_H
#define KWIN_OVERLAYWINDOW_H

#include <kwin_export.h>

#include <QRegion>
#include <xcb/xcb.h>

namespace KWin
{

class KWIN_EXPORT OverlayWindow
{
public:
    virtual ~OverlayWindow();
    /// Creates XComposite overlay window, call initOverlay() afterwards
    virtual bool create() = 0;
    /// Init overlay and the destination window in it
    virtual void setup(xcb_window_t window) = 0;
    virtual void show() = 0;
    virtual void hide() = 0; // hides and resets overlay window
    virtual void setShape(const QRegion& reg) = 0;
    virtual void resize(const QSize& size) = 0;
    /// Destroys XComposite overlay window
    virtual void destroy() = 0;
    virtual xcb_window_t window() const = 0;
    virtual bool isVisible() const = 0;
    virtual void setVisibility(bool visible) = 0;

protected:
    OverlayWindow();
};

} // namespace

#endif // KWIN_OVERLAYWINDOW_H
