/*
    SPDX-FileCopyrightText: 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "client.h"
#include "event.h"
#include "geo.h"
#include "root_info_filter.h"
#include "stacking.h"
#include "window_find.h"

#include "utils/memory.h"
#include "win/activation.h"

#include <NETWM>
#include <memory>
#include <xcb/xcb.h>

namespace KWin::win::x11
{

/**
 * NET WM Protocol handler class
 */
template<typename Space>
class root_info : public NETRootInfo
{
public:
    using window_t = typename Space::x11_window;

    static std::unique_ptr<root_info> create(Space& space)
    {
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
            qCDebug(KWIN_CORE) << "Error occurred while lowering support window: "
                               << error->error_code;
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
            NET::WM2GTKFrameExtents |
            NET::WM2GTKShowWindowMenu;
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

        return std::make_unique<root_info<Space>>(space,
                                                  supportWindow,
                                                  "KWin",
                                                  properties,
                                                  types,
                                                  states,
                                                  properties2,
                                                  actions,
                                                  kwinApp()->x11ScreenNumber());
    }

    root_info(Space& space,
              xcb_window_t w,
              const char* name,
              NET::Properties properties,
              NET::WindowTypes types,
              NET::States states,
              NET::Properties2 properties2,
              NET::Actions actions,
              int scr = -1)
        : NETRootInfo(connection(), w, name, properties, types, states, properties2, actions, scr)
        , space{space}
        , m_activeWindow(activeWindow())
        , m_eventFilter(std::make_unique<root_info_filter<root_info<Space>>>(this))
    {
    }

    ~root_info() override
    {
        xcb_destroy_window(connection(), supportWindow());
    }

    Space& space;
    xcb_window_t m_activeWindow;

protected:
    void changeNumberOfDesktops(int n) override
    {
        space.virtual_desktop_manager->setCount(n);
    }

    void changeCurrentDesktop(int d) override
    {
        space.virtual_desktop_manager->setCurrent(d);
    }

    void changeActiveWindow(xcb_window_t w,
                            NET::RequestSource src,
                            xcb_timestamp_t timestamp,
                            xcb_window_t active_window) override
    {
        if (auto c = find_controlled_window<window_t>(space, predicate_match::window, w)) {
            if (timestamp == XCB_CURRENT_TIME)
                timestamp = c->userTime();
            if (src != NET::FromApplication && src != FromTool)
                src = NET::FromTool;

            if (src == NET::FromTool) {
                force_activate_window(space, *c);
            } else if (c == most_recently_activated_window(space)) {
                return; // WORKAROUND? With > 1 plasma activities, we cause this ourselves. bug
                        // #240673
            } else {    // NET::FromApplication
                window_t* c2;
                if (allow_window_activation(space, c, timestamp, false, true)) {
                    activate_window(space, *c);
                }

                // if activation of the requestor's window would be allowed, allow activation too
                else if (active_window != XCB_WINDOW_NONE
                         && (c2 = find_controlled_window<window_t>(
                                 space, predicate_match::window, active_window))
                             != nullptr
                         && allow_window_activation(
                             space,
                             c2,
                             timestampCompare(timestamp,
                                              c2->userTime() > 0 ? timestamp : c2->userTime()),
                             false,
                             true)) {
                    activate_window(space, *c);
                } else
                    win::set_demands_attention(c, true);
            }
        }
    }

    void closeWindow(xcb_window_t w) override
    {
        if (auto win = find_controlled_window<window_t>(space, predicate_match::window, w)) {
            win->closeWindow();
        }
    }

    void moveResize(xcb_window_t w, int x_root, int y_root, unsigned long direction) override
    {
        if (auto win = find_controlled_window<window_t>(space, predicate_match::window, w)) {
            // otherwise grabbing may have old timestamp - this message should include timestamp
            kwinApp()->update_x11_time_from_clock();
            x11::net_move_resize(win, x_root, y_root, static_cast<Direction>(direction));
        }
    }

    void moveResizeWindow(xcb_window_t w, int flags, int x, int y, int width, int height) override
    {
        if (auto win = find_controlled_window<window_t>(space, predicate_match::window, w)) {
            x11::net_move_resize_window(win, flags, x, y, width, height);
        }
    }

    void showWindowMenu(xcb_window_t w, int /*device_id*/, int x_root, int y_root) override
    {
        if (auto win = find_controlled_window<window_t>(space, predicate_match::window, w)) {
            auto pos = QPoint(x_root, y_root);
            space.user_actions_menu->show(QRect(pos, pos), win);
        }
    }

    void gotPing(xcb_window_t w, xcb_timestamp_t timestamp) override
    {
        if (auto c = find_controlled_window<window_t>(space, predicate_match::window, w))
            x11::pong(c, timestamp);
    }

    void restackWindow(xcb_window_t w,
                       RequestSource source,
                       xcb_window_t above,
                       int detail,
                       xcb_timestamp_t timestamp) override
    {
        if (auto c = find_controlled_window<window_t>(space, predicate_match::window, w)) {
            if (timestamp == XCB_CURRENT_TIME) {
                timestamp = c->userTime();
            }
            if (source != NET::FromApplication && source != FromTool) {
                source = NET::FromTool;
            }
            x11::restack_window(c, above, detail, source, timestamp, true);
        }
    }

    void changeShowingDesktop(bool showing) override
    {
        set_showing_desktop(space, showing);
    }

private:
    std::unique_ptr<root_info_filter<root_info<Space>>> m_eventFilter;
};

}
