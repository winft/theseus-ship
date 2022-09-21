/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

#include <QMargins>
#include <QRect>
#include <QSize>

namespace KWin::win
{

struct window_geometry {
    QPoint pos() const
    {
        return frame.topLeft();
    }

    QSize size() const
    {
        return frame.size();
    }

    /// Excludes invisible portions, e.g. server-side and client-side drop shadows, etc.
    QRect frame;
    QMargins client_frame_extents;
    bool has_in_content_deco{false};

    struct {
        int block{0};
        pending_geometry pending{pending_geometry::none};

        QRect frame;
        maximize_mode max_mode{maximize_mode::restore};
        bool fullscreen{false};

        struct {
            QMargins deco_margins;
            QMargins client_frame_extents;
        } original;
    } update;

    /**
     * Used to store and retrieve frame geometry values when certain geometry-transforming
     * actions are triggered and later reversed again. For example when a window has been
     * maximized and later again unmaximized.
     */
    struct {
        QRect max;
    } restore;
};

}
