/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control.h"
#include "deco/renderer.h"
#include "space_window_release.h"

#include "base/logging.h"

#include <NETWM>
#include <QMargins>
#include <QRect>
#include <QRegion>
#include <cassert>
#include <memory>

namespace KWin::win
{

template<typename Win>
class remnant
{
public:
    remnant(Win& win)
        : win{win}
    {
    }

    ~remnant()
    {
        if (refcount != 0) {
            qCCritical(KWIN_CORE) << "Remnant on destroy still with" << refcount << "refs.";
        }
        delete_window_from_space(win.space, &win);
    }

    void ref()
    {
        ++refcount;
    }

    void unref()
    {
        --refcount;
    }

    void discard()
    {
        refcount = 0;
        delete &win;
    }

    void layout_decoration_rects(QRect& left, QRect& top, QRect& right, QRect& bottom) const
    {
        left = decoration_left;
        top = decoration_top;
        right = decoration_right;
        bottom = decoration_bottom;
    }

    QMargins frame_margins;
    QRegion render_region;

    int refcount{1};

    int desk;

    xcb_window_t frame{XCB_WINDOW_NONE};

    bool no_border{true};
    QRect decoration_left;
    QRect decoration_right;
    QRect decoration_top;
    QRect decoration_bottom;

    bool minimized{false};

    std::unique_ptr<deco::renderer> decoration_renderer;
    double opacity{1};
    NET::WindowType window_type{NET::Unknown};
    QByteArray window_role;
    QString caption;

    bool fullscreen{false};
    bool keep_above{false};
    bool keep_below{false};
    bool was_active{false};

    bool was_x11_client{false};
    bool was_wayland_client{false};

    bool was_group_transient{false};
    bool was_popup_window{false};
    bool was_outline{false};
    bool was_lock_screen{false};

    double buffer_scale{1};

    std::unique_ptr<win::control> control;

private:
    Win& win;
};

}
