/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QRect>

namespace KWin::win::x11::net
{

struct point {
    point() = default;
    point(QPoint const& p)
        : x(p.x())
        , y(p.y())
    {
    }

    QPoint toPoint() const
    {
        return {x, y};
    }

    int x{0};
    int y{0};
};

struct size {
    size() = default;
    size(QSize const& size)
        : width(size.width())
        , height(size.height())
    {
    }

    QSize toSize() const
    {
        return {width, height};
    }

    int width{0};
    int height{0};
};

struct rect {
    rect() = default;
    rect(QRect const& rect)
        : pos(rect.topLeft())
        , size(rect.size())
    {
    }

    QRect toRect() const
    {
        return QRect(pos.x, pos.y, size.width, size.height);
    }

    point pos;
    net::size size;
};

struct extended_strut {
    // Left border of the strut, width and range.
    int left_width{0};
    int left_start{0};
    int left_end{0};

    // Right border of the strut, width and range.
    int right_width{0};
    int right_start{0};
    int right_end{0};

    // Top border of the strut, width and range.
    int top_width{0};
    int top_start{0};
    int top_end{0};

    // Bottom border of the strut, width and range.
    int bottom_width{0};
    int bottom_start{0};
    int bottom_end{0};
};

/**
   @deprecated use extended_strut

   Simple strut class for NET classes.

   This class is a convenience class defining a strut with left, right, top and
   bottom border values.  The existence of this class is to keep the implementation
   from being dependent on a separate framework/library. See the _NET_WM_STRUT
   property in the NETWM spec.
**/

struct strut {
    int left{0};
    int right{0};
    int top{0};
    int bottom{0};
};

struct icon {
    net::size size;

    /**
       Image data for the icon.  This is an array of 32bit packed CARDINAL ARGB
       with high byte being A, low byte being B. First two bytes are width, height.
       Data is in rows, left to right and top to bottom.
    **/
    unsigned char* data{nullptr};
};

/**
   Simple multiple monitor topology class for NET classes.

   This class is a convenience class, defining a multiple monitor topology
   for fullscreen applications that wish to be present on more than one
   monitor/head. As per the _NET_WM_FULLSCREEN_MONITORS hint in the EWMH spec,
   this topology consists of 4 monitor indices such that the bounding rectangle
   is defined by the top edge of the top monitor, the bottom edge of the bottom
   monitor, the left edge of the left monitor, and the right edge of the right
   monitor. See the _NET_WM_FULLSCREEN_MONITORS hint in the EWMH spec.
**/
struct fullscreen_monitors {
    /**
       Constructor to initialize this struct to -1,0,0,0 (an initialized,
       albeit invalid, topology).
    **/
    fullscreen_monitors()
        : top(-1)
        , bottom(0)
        , left(0)
        , right(0)
    {
    }

    /**
       Convenience check to make sure that we are not holding the initial (invalid)
       values. Note that we don't want to call this isValid() because we're not
       actually validating the monitor topology here, but merely that our initial
       values were overwritten at some point by real (non-negative) monitor indices.
    **/
    bool isSet() const
    {
        return (top != -1);
    }

    // Monitor index whose top border defines the top edge of the topology.
    int top;

    // Monitor index whose bottom border defines the bottom edge of the topology.
    int bottom;

    // Monitor index whose left border defines the left edge of the topology.
    int left;

    // Monitor index whose right border defines the right edge of the topology.
    int right;
};

}
