/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"
#include "window_area.h"

namespace KWin::win
{

template<typename Win>
QRect electric_border_maximize_geometry(Win const* win, QPoint pos, int subspace)
{
    if (win->control->electric == win::quicktiles::maximize) {
        if (win->maximizeMode() == maximize_mode::full) {
            return win->geo.restore.max;
        } else {
            return space_window_area(win->space, area_option::maximize, pos, subspace);
        }
    }

    auto ret = space_window_area(win->space, area_option::maximize, pos, subspace);

    if (flags(win->control->electric & win::quicktiles::left)) {
        ret.setRight(ret.left() + ret.width() / 2 - 1);
    } else if (flags(win->control->electric & win::quicktiles::right)) {
        ret.setLeft(ret.right() - (ret.width() - ret.width() / 2) + 1);
    }

    if (flags(win->control->electric & win::quicktiles::top)) {
        ret.setBottom(ret.top() + ret.height() / 2 - 1);
    } else if (flags(win->control->electric & win::quicktiles::bottom)) {
        ret.setTop(ret.bottom() - (ret.height() - ret.height() / 2) + 1);
    }

    return ret;
}

template<typename Win>
void set_electric_maximizing(Win* win, bool maximizing)
{
    win->control->electric_maximizing = maximizing;

    if (maximizing) {
        auto max_geo = electric_border_maximize_geometry(
            win, win->space.input->cursor->pos(), get_subspace(*win));
        win->space.outline->show(max_geo, win->control->move_resize.geometry);
    } else {
        win->space.outline->hide();
    }

    elevate(win, maximizing);
}

template<typename Win>
void delayed_electric_maximize(Win* win)
{
    auto timer = win->control->electric_maximizing_delay;
    if (!timer) {
        timer = new QTimer(win->qobject.get());
        timer->setInterval(250);
        timer->setSingleShot(true);
        QObject::connect(timer, &QTimer::timeout, [win]() {
            if (is_move(win)) {
                set_electric_maximizing(win, win->control->electric != quicktiles::none);
            }
        });
    }
    timer->start();
}

template<typename Win>
void set_electric(Win* win, quicktiles tiles)
{
    if (tiles != quicktiles::maximize) {
        // sanitize the mode, ie. simplify "invalid" combinations
        if ((tiles & quicktiles::horizontal) == quicktiles::horizontal) {
            tiles &= ~quicktiles::horizontal;
        }
        if ((tiles & quicktiles::vertical) == quicktiles::vertical) {
            tiles &= ~quicktiles::vertical;
        }
    }
    win->control->electric = tiles;
}

}
