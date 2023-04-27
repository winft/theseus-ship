/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "geo.h"

#include "net/win_info.h"
#include "win/desktop_space.h"

#include <xcb/xcb.h>

namespace KWin::win::x11
{

template<typename Win>
class win_info : public net::win_info
{
public:
    win_info(Win* window,
             xcb_window_t xcb_win,
             xcb_window_t rwin,
             net::Properties properties,
             net::Properties2 properties2)
        : net::win_info(window->space.base.x11_data.connection,
                        xcb_win,
                        rwin,
                        properties,
                        properties2,
                        net::WindowManager)
        , window(window)
    {
    }

    void changeDesktop(int desktop) override
    {
        send_window_to_desktop(window->space, window, desktop, true);
    }

    void changeFullscreenMonitors(net::fullscreen_monitors topology) override
    {
        update_fullscreen_monitors(window, topology);
    }

    void changeState(net::States state, net::States mask) override
    {
        // We don't support large desktops, ignore clients are not allowed to change this directly.
        // For safety, clear all other bits.
        mask &= ~net::Sticky;
        mask &= ~net::Hidden;
        state &= mask;

        if ((mask & net::FullScreen) != 0 && (state & net::FullScreen) == 0) {
            window->setFullScreen(false, false);
        }

        if ((mask & net::Max) == net::Max) {
            set_maximize(window, state & net::MaxVert, state & net::MaxHoriz);
        } else if (mask & net::MaxVert) {
            set_maximize(window,
                         state & net::MaxVert,
                         flags(window->maximizeMode() & maximize_mode::horizontal));
        } else if (mask & net::MaxHoriz) {
            set_maximize(window,
                         flags(window->maximizeMode() & maximize_mode::vertical),
                         state & net::MaxHoriz);
        }

        if (mask & net::KeepAbove) {
            set_keep_above(window, (state & net::KeepAbove) != 0);
        }
        if (mask & net::KeepBelow) {
            set_keep_below(window, (state & net::KeepBelow) != 0);
        }
        if (mask & net::SkipTaskbar) {
            set_original_skip_taskbar(window, (state & net::SkipTaskbar) != 0);
        }
        if (mask & net::SkipPager) {
            set_skip_pager(window, (state & net::SkipPager) != 0);
        }
        if (mask & net::SkipSwitcher) {
            set_skip_switcher(window, (state & net::SkipSwitcher) != 0);
        }
        if (mask & net::DemandsAttention) {
            set_demands_attention(window, (state & net::DemandsAttention) != 0);
        }
        if (mask & net::Modal) {
            window->transient->set_modal((state & net::Modal) != 0);
        }
        // Unsetting fullscreen first, setting it last (because e.g. maximize works only for
        // !isFullScreen()).
        if ((mask & net::FullScreen) != 0 && (state & net::FullScreen) != 0) {
            window->setFullScreen(true, false);
        }
    }

    void disable()
    {
        // Only used when the object is passed to a remnant.
        window = nullptr;
    }

private:
    Win* window;
};

}
