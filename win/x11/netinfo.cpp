/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>
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
#include "netinfo.h"

#include "root_info_filter.h"
#include "window_find.h"

#include "win/controlling.h"
#include "win/desktop_space.h"
#include "win/move.h"
#include "win/space.h"
#include "win/stacking.h"
#include "win/virtual_desktops.h"
#include "win/x11/event.h"
#include "win/x11/geo.h"
#include "win/x11/stacking.h"
#include "win/x11/window.h"

#include <QDebug>

namespace KWin::win::x11
{
root_info* root_info::s_self = nullptr;

root_info* root_info::create(win::space& space)
{
    Q_ASSERT(!s_self);
    xcb_window_t supportWindow = xcb_generate_id(connection());
    const uint32_t values[] = {true};
    xcb_create_window(connection(),
                      XCB_COPY_FROM_PARENT,
                      supportWindow,
                      KWin::rootWindow(),
                      0,
                      0,
                      1,
                      1,
                      0,
                      XCB_COPY_FROM_PARENT,
                      XCB_COPY_FROM_PARENT,
                      XCB_CW_OVERRIDE_REDIRECT,
                      values);
    const uint32_t lowerValues[] = {XCB_STACK_MODE_BELOW}; // See usage in layers.cpp
    // we need to do the lower window with a roundtrip, otherwise NETRootInfo is not functioning
    unique_cptr<xcb_generic_error_t> error(xcb_request_check(
        connection(),
        xcb_configure_window_checked(
            connection(), supportWindow, XCB_CONFIG_WINDOW_STACK_MODE, lowerValues)));
    if (error) {
        qCDebug(KWIN_CORE) << "Error occurred while lowering support window: " << error->error_code;
    }

    // clang-format off
    const NET::Properties properties = NET::Supported |
        NET::SupportingWMCheck |
        NET::ClientList |
        NET::ClientListStacking |
        NET::DesktopGeometry |
        NET::NumberOfDesktops |
        NET::CurrentDesktop |
        NET::ActiveWindow |
        NET::WorkArea |
        NET::CloseWindow |
        NET::DesktopNames |
        NET::WMName |
        NET::WMVisibleName |
        NET::WMDesktop |
        NET::WMWindowType |
        NET::WMState |
        NET::WMStrut |
        NET::WMIconGeometry |
        NET::WMIcon |
        NET::WMPid |
        NET::WMMoveResize |
        NET::WMFrameExtents |
        NET::WMPing;
    const NET::WindowTypes types = NET::NormalMask |
        NET::DesktopMask |
        NET::DockMask |
        NET::ToolbarMask |
        NET::MenuMask |
        NET::DialogMask |
        NET::OverrideMask |
        NET::UtilityMask |
        NET::SplashMask; // No compositing window types here unless we support them also as managed window types
    const NET::States states = NET::Modal |
        //NET::Sticky | // Large desktops not supported (and probably never will be)
        NET::MaxVert |
        NET::MaxHoriz |
        // NET::Shaded | // Shading not supported
        NET::SkipTaskbar |
        NET::KeepAbove |
        //NET::StaysOnTop | // The same like KeepAbove
        NET::SkipPager |
        NET::Hidden |
        NET::FullScreen |
        NET::KeepBelow |
        NET::DemandsAttention |
        NET::SkipSwitcher |
        NET::Focused;
    NET::Properties2 properties2 = NET::WM2UserTime |
        NET::WM2StartupId |
        NET::WM2AllowedActions |
        NET::WM2RestackWindow |
        NET::WM2MoveResizeWindow |
        NET::WM2ExtendedStrut |
        NET::WM2KDETemporaryRules |
        NET::WM2ShowingDesktop |
        NET::WM2DesktopLayout |
        NET::WM2FullPlacement |
        NET::WM2FullscreenMonitors |
        NET::WM2KDEShadow |
        NET::WM2OpaqueRegion |
        NET::WM2GTKFrameExtents;
    const NET::Actions actions = NET::ActionMove |
        NET::ActionResize |
        NET::ActionMinimize |
        // NET::ActionShade | // Shading not supported
        //NET::ActionStick | // Sticky state is not supported
        NET::ActionMaxVert |
        NET::ActionMaxHoriz |
        NET::ActionFullScreen |
        NET::ActionChangeDesktop |
        NET::ActionClose;
    // clang-format on

    s_self = new root_info(space,
                           supportWindow,
                           "KWin",
                           properties,
                           types,
                           states,
                           properties2,
                           actions,
                           kwinApp()->x11ScreenNumber());
    return s_self;
}

void root_info::destroy()
{
    if (!s_self) {
        return;
    }
    xcb_window_t supportWindow = s_self->supportWindow();
    delete s_self;
    s_self = nullptr;
    xcb_destroy_window(connection(), supportWindow);
}

root_info::root_info(win::space& space,
                     xcb_window_t w,
                     const char* name,
                     NET::Properties properties,
                     NET::WindowTypes types,
                     NET::States states,
                     NET::Properties2 properties2,
                     NET::Actions actions,
                     int scr)
    : NETRootInfo(connection(), w, name, properties, types, states, properties2, actions, scr)
    , space{space}
    , m_activeWindow(activeWindow())
    , m_eventFilter(std::make_unique<root_info_filter>(this))
{
}

void root_info::changeNumberOfDesktops(int n)
{
    space.virtual_desktop_manager->setCount(n);
}

void root_info::changeCurrentDesktop(int d)
{
    space.virtual_desktop_manager->setCurrent(d);
}

void root_info::changeActiveWindow(xcb_window_t w,
                                   NET::RequestSource src,
                                   xcb_timestamp_t timestamp,
                                   xcb_window_t active_window)
{
    if (auto c = find_controlled_window<x11::window>(space, predicate_match::window, w)) {
        if (timestamp == XCB_CURRENT_TIME)
            timestamp = c->userTime();
        if (src != NET::FromApplication && src != FromTool)
            src = NET::FromTool;

        if (src == NET::FromTool) {
            force_activate_window(space, c);
        } else if (c == most_recently_activated_window(space)) {
            return; // WORKAROUND? With > 1 plasma activities, we cause this ourselves. bug #240673
        } else {    // NET::FromApplication
            x11::window* c2;
            if (space.allowClientActivation(c, timestamp, false, true))
                activate_window(space, c);
            // if activation of the requestor's window would be allowed, allow activation too
            else if (active_window != XCB_WINDOW_NONE
                     && (c2 = find_controlled_window<x11::window>(
                             space, predicate_match::window, active_window))
                         != nullptr
                     && space.allowClientActivation(
                         c2,
                         timestampCompare(timestamp,
                                          c2->userTime() > 0 ? timestamp : c2->userTime()),
                         false,
                         true)) {
                activate_window(space, c);
            } else
                win::set_demands_attention(c, true);
        }
    }
}

void root_info::restackWindow(xcb_window_t w,
                              RequestSource src,
                              xcb_window_t above,
                              int detail,
                              xcb_timestamp_t timestamp)
{
    if (auto c = find_controlled_window<x11::window>(space, predicate_match::window, w)) {
        if (timestamp == XCB_CURRENT_TIME)
            timestamp = c->userTime();
        if (src != NET::FromApplication && src != FromTool)
            src = NET::FromTool;
        win::x11::restack_window(c, above, detail, src, timestamp, true);
    }
}

void root_info::closeWindow(xcb_window_t w)
{
    if (auto win = find_controlled_window<x11::window>(space, predicate_match::window, w)) {
        win->closeWindow();
    }
}

void root_info::moveResize(xcb_window_t w, int x_root, int y_root, unsigned long direction)
{
    auto c = find_controlled_window<x11::window>(space, predicate_match::window, w);
    if (c) {
        // otherwise grabbing may have old timestamp - this message should include timestamp
        kwinApp()->update_x11_time_from_clock();
        win::x11::net_move_resize(c, x_root, y_root, static_cast<Direction>(direction));
    }
}

void root_info::moveResizeWindow(xcb_window_t w, int flags, int x, int y, int width, int height)
{
    auto c = find_controlled_window<x11::window>(space, predicate_match::window, w);
    if (c)
        win::x11::net_move_resize_window(c, flags, x, y, width, height);
}

void root_info::gotPing(xcb_window_t w, xcb_timestamp_t timestamp)
{
    if (auto c = find_controlled_window<x11::window>(space, predicate_match::window, w))
        win::x11::pong(c, timestamp);
}

void root_info::changeShowingDesktop(bool showing)
{
    space.setShowingDesktop(showing);
}

void root_info::setActiveClient(Toplevel* window)
{
    auto const xcb_win
        = window ? static_cast<xcb_window_t>(window->xcb_window) : xcb_window_t{XCB_WINDOW_NONE};
    if (m_activeWindow == xcb_win) {
        return;
    }
    m_activeWindow = xcb_win;
    setActiveWindow(m_activeWindow);
}

// ****************************************
// win_info
// ****************************************

win_info::win_info(win::x11::window* c,
                   xcb_window_t window,
                   xcb_window_t rwin,
                   NET::Properties properties,
                   NET::Properties2 properties2)
    : NETWinInfo(connection(), window, rwin, properties, properties2, NET::WindowManager)
    , m_client(c)
{
}

void win_info::changeDesktop(int desktop)
{
    send_window_to_desktop(m_client->space, m_client, desktop, true);
}

void win_info::changeFullscreenMonitors(NETFullscreenMonitors topology)
{
    win::x11::update_fullscreen_monitors(m_client, topology);
}

void win_info::changeState(NET::States state, NET::States mask)
{
    mask &= ~NET::Sticky; // KWin doesn't support large desktops, ignore
    mask &= ~NET::Hidden; // clients are not allowed to change this directly
    state &= mask;        // for safety, clear all other bits

    if ((mask & NET::FullScreen) != 0 && (state & NET::FullScreen) == 0)
        m_client->setFullScreen(false, false);
    if ((mask & NET::Max) == NET::Max)
        win::set_maximize(m_client, state & NET::MaxVert, state & NET::MaxHoriz);
    else if (mask & NET::MaxVert)
        win::set_maximize(m_client,
                          state & NET::MaxVert,
                          flags(m_client->maximizeMode() & win::maximize_mode::horizontal));
    else if (mask & NET::MaxHoriz)
        win::set_maximize(m_client,
                          flags(m_client->maximizeMode() & win::maximize_mode::vertical),
                          state & NET::MaxHoriz);

    if (mask & NET::KeepAbove)
        win::set_keep_above(m_client, (state & NET::KeepAbove) != 0);
    if (mask & NET::KeepBelow)
        win::set_keep_below(m_client, (state & NET::KeepBelow) != 0);
    if (mask & NET::SkipTaskbar)
        win::set_original_skip_taskbar(m_client, (state & NET::SkipTaskbar) != 0);
    if (mask & NET::SkipPager)
        win::set_skip_pager(m_client, (state & NET::SkipPager) != 0);
    if (mask & NET::SkipSwitcher)
        win::set_skip_switcher(m_client, (state & NET::SkipSwitcher) != 0);
    if (mask & NET::DemandsAttention)
        win::set_demands_attention(m_client, (state & NET::DemandsAttention) != 0);
    if (mask & NET::Modal)
        m_client->transient()->set_modal((state & NET::Modal) != 0);
    // unsetting fullscreen first, setting it last (because e.g. maximize works only for
    // !isFullScreen() )
    if ((mask & NET::FullScreen) != 0 && (state & NET::FullScreen) != 0)
        m_client->setFullScreen(true, false);
}

void win_info::disable()
{
    m_client = nullptr; // only used when the object is passed to Deleted
}

}
