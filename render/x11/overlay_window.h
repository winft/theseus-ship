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
#pragma once

#include "base/x11/event_filter.h"
#include "kwin_export.h"

#include <QRegion>
#include <xcb/xcb.h>

namespace KWin::render::x11
{

class KWIN_EXPORT overlay_window : public base::x11::event_filter
{
public:
    overlay_window();
    ~overlay_window();

    /// Creates XComposite overlay window, call initOverlay() afterwards
    bool create();

    /// Init overlay and the destination window in it
    void setup(xcb_window_t window);
    void show();

    /// Hides and resets overlay window
    void hide();
    void setShape(const QRegion& reg);
    void resize(const QSize& size);

    /// Destroys XComposite overlay window
    void destroy();
    xcb_window_t window() const;
    bool isVisible() const;
    void setVisibility(bool visible);

    bool event(xcb_generic_event_t* event) override;

private:
    void setNoneBackgroundPixmap(xcb_window_t window);
    void setupInputShape(xcb_window_t window);

    bool m_visible;

    // For showOverlay()
    bool m_shown;

    QRegion m_shape;
    xcb_window_t m_window;
};

}
