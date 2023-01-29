/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "geo.h"

#include "win/desktop_space.h"

#include <NETWM>
#include <xcb/xcb.h>

namespace KWin::win::x11
{

/**
 * NET WM Protocol handler class
 */
template<typename Win>
class win_info : public NETWinInfo
{
public:
    win_info(Win* window,
             xcb_window_t xcb_win,
             xcb_window_t rwin,
             NET::Properties properties,
             NET::Properties2 properties2)
        : NETWinInfo(window->space.base.x11_data.connection,
                     xcb_win,
                     rwin,
                     properties,
                     properties2,
                     NET::WindowManager)
        , window(window)
    {
    }

    void changeDesktop(int desktop) override
    {
        send_window_to_desktop(window->space, window, desktop, true);
    }

    void changeFullscreenMonitors(NETFullscreenMonitors topology) override
    {
        update_fullscreen_monitors(window, topology);
    }

    void changeState(NET::States state, NET::States mask) override
    {
        // We don't support large desktops, ignore clients are not allowed to change this directly.
        // For safety, clear all other bits.
        mask &= ~NET::Sticky;
        mask &= ~NET::Hidden;
        state &= mask;

        if ((mask & NET::FullScreen) != 0 && (state & NET::FullScreen) == 0) {
            window->setFullScreen(false, false);
        }

        if ((mask & NET::Max) == NET::Max) {
            set_maximize(window, state & NET::MaxVert, state & NET::MaxHoriz);
        } else if (mask & NET::MaxVert) {
            set_maximize(window,
                         state & NET::MaxVert,
                         flags(window->maximizeMode() & maximize_mode::horizontal));
        } else if (mask & NET::MaxHoriz) {
            set_maximize(window,
                         flags(window->maximizeMode() & maximize_mode::vertical),
                         state & NET::MaxHoriz);
        }

        if (mask & NET::KeepAbove) {
            set_keep_above(window, (state & NET::KeepAbove) != 0);
        }
        if (mask & NET::KeepBelow) {
            set_keep_below(window, (state & NET::KeepBelow) != 0);
        }
        if (mask & NET::SkipTaskbar) {
            set_original_skip_taskbar(window, (state & NET::SkipTaskbar) != 0);
        }
        if (mask & NET::SkipPager) {
            set_skip_pager(window, (state & NET::SkipPager) != 0);
        }
        if (mask & NET::SkipSwitcher) {
            set_skip_switcher(window, (state & NET::SkipSwitcher) != 0);
        }
        if (mask & NET::DemandsAttention) {
            set_demands_attention(window, (state & NET::DemandsAttention) != 0);
        }
        if (mask & NET::Modal) {
            window->transient->set_modal((state & NET::Modal) != 0);
        }
        // Unsetting fullscreen first, setting it last (because e.g. maximize works only for
        // !isFullScreen()).
        if ((mask & NET::FullScreen) != 0 && (state & NET::FullScreen) != 0) {
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
