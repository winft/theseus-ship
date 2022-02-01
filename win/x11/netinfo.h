/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2009 Lucas Murray <lmurray@undefinedfire.com>
Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

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

#include <kwin_export.h>

#include <NETWM>

#include <memory>
#include <xcb/xcb.h>

namespace KWin
{
class Toplevel;

namespace win::x11
{
class root_info_filter;
class window;

/**
 * NET WM Protocol handler class
 */
class KWIN_EXPORT root_info : public NETRootInfo
{
public:
    static root_info* create();
    static void destroy();

    void setActiveClient(Toplevel* window);

protected:
    void changeNumberOfDesktops(int n) override;
    void changeCurrentDesktop(int d) override;
    void changeActiveWindow(xcb_window_t w,
                            NET::RequestSource src,
                            xcb_timestamp_t timestamp,
                            xcb_window_t active_window) override;
    void closeWindow(xcb_window_t w) override;
    void moveResize(xcb_window_t w, int x_root, int y_root, unsigned long direction) override;
    void moveResizeWindow(xcb_window_t w, int flags, int x, int y, int width, int height) override;
    void gotPing(xcb_window_t w, xcb_timestamp_t timestamp) override;
    void restackWindow(xcb_window_t w,
                       RequestSource source,
                       xcb_window_t above,
                       int detail,
                       xcb_timestamp_t timestamp) override;
    void changeShowingDesktop(bool showing) override;

private:
    root_info(xcb_window_t w,
              const char* name,
              NET::Properties properties,
              NET::WindowTypes types,
              NET::States states,
              NET::Properties2 properties2,
              NET::Actions actions,
              int scr = -1);
    static root_info* s_self;
    friend root_info* rootInfo();

    xcb_window_t m_activeWindow;
    std::unique_ptr<root_info_filter> m_eventFilter;
};

inline root_info* rootInfo()
{
    return root_info::s_self;
}

/**
 * NET WM Protocol handler class
 */
class KWIN_EXPORT win_info : public NETWinInfo
{
public:
    win_info(win::x11::window* c,
             xcb_window_t window,
             xcb_window_t rwin,
             NET::Properties properties,
             NET::Properties2 properties2);
    void changeDesktop(int desktop) override;
    void changeFullscreenMonitors(NETFullscreenMonitors topology) override;
    void changeState(NET::States state, NET::States mask) override;
    void disable();

private:
    win::x11::window* m_client;
};

}
}
