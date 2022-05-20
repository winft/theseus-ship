/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <NETWM>
#include <QMargins>
#include <QRect>
#include <QRegion>

#include <memory>
#include <vector>

namespace KWin
{
class Toplevel;

namespace win
{

namespace deco
{
class renderer;
}

class control;

class KWIN_EXPORT remnant
{
public:
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

    deco::renderer* decoration_renderer{nullptr};
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

    Toplevel* win;

    remnant(Toplevel* win, Toplevel* source);
    ~remnant();

    void ref();
    void unref();
    void discard();

    bool was_transient() const;
    bool has_lead(Toplevel const* toplevel) const;
    void lead_closed(Toplevel* window, Toplevel* deleted);

    void layout_decoration_rects(QRect& left, QRect& top, QRect& right, QRect& bottom) const;
};

}
}
