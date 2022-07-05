/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "geo.h"
#include "scene.h"
#include "types.h"
#include "window_area.h"

namespace KWin::win
{

template<typename Win>
QSize constrain_and_adjust_size(Win* win, QSize const& size)
{
    auto width = size.width();
    auto height = size.height();

    auto const area = space_window_area(win->space, WorkArea, win);

    width = std::min(width, area.width());
    height = std::min(height, area.height());

    // checks size constraints, including min/max size
    return adjusted_frame_size(win, QSize(width, height), win::size_mode::any);
}

template<typename Win>
void constrained_resize(Win* win, QSize const& size)
{
    win->setFrameGeometry(
        QRect(win->geometry_update.frame.topLeft(), constrain_and_adjust_size(win, size)));
}

template<typename Win>
void grow_horizontal(Win* win)
{
    if (!win->isResizable()) {
        return;
    }

    auto frame_geo = win->frameGeometry();
    frame_geo.setRight(win->space.packPositionRight(win, frame_geo.right(), true));
    auto const adjsize = adjusted_frame_size(win, frame_geo.size(), size_mode::fixed_width);

    if (win->frameGeometry().size() == adjsize && frame_geo.size() != adjsize
        && win->resizeIncrements().width() > 1) {
        // Grow by increment.
        auto const grown_right = win->space.packPositionRight(
            win, frame_geo.right() + win->resizeIncrements().width() - 1, true);

        // Check that it hasn't grown outside of the area, due to size increments.
        // TODO this may be wrong?
        auto const area = space_window_area(
            win->space,
            MovementArea,
            QPoint((win->pos().x() + grown_right) / 2, win->frameGeometry().center().y()),
            win->desktop());
        if (area.right() >= grown_right) {
            frame_geo.setRight(grown_right);
        }
    }

    frame_geo.setSize(adjusted_frame_size(win, frame_geo.size(), size_mode::fixed_width));
    frame_geo.setSize(adjusted_frame_size(win, frame_geo.size(), size_mode::fixed_height));

    // May cause leave event.
    win->space.focusMousePos = input::get_cursor()->pos();
    win->setFrameGeometry(frame_geo);
}

template<typename Win>
void shrink_horizontal(Win* win)
{
    if (!win->isResizable()) {
        return;
    }

    auto geom = win->frameGeometry();
    geom.setRight(win->space.packPositionLeft(win, geom.right(), false));

    if (geom.width() <= 1) {
        return;
    }

    geom.setSize(adjusted_frame_size(win, geom.size(), size_mode::fixed_width));

    // TODO(romangg): Magic number 20. Why?
    if (geom.width() > 20) {
        // May cause leave event.
        win->space.focusMousePos = input::get_cursor()->pos();
        win->setFrameGeometry(geom);
    }
}

template<typename Win>
void grow_vertical(Win* win)
{
    if (!win->isResizable()) {
        return;
    }

    auto frame_geo = win->frameGeometry();
    frame_geo.setBottom(win->space.packPositionDown(win, frame_geo.bottom(), true));
    auto adjsize = adjusted_frame_size(win, frame_geo.size(), size_mode::fixed_height);

    if (win->frameGeometry().size() == adjsize && frame_geo.size() != adjsize
        && win->resizeIncrements().height() > 1) {
        // Grow by increment.
        auto const newbottom = win->space.packPositionDown(
            win, frame_geo.bottom() + win->resizeIncrements().height() - 1, true);

        // check that it hasn't grown outside of the area, due to size increments
        auto const area = space_window_area(
            win->space,
            MovementArea,
            QPoint(win->frameGeometry().center().x(), (win->pos().y() + newbottom) / 2),
            win->desktop());
        if (area.bottom() >= newbottom) {
            frame_geo.setBottom(newbottom);
        }
    }

    frame_geo.setSize(adjusted_frame_size(win, frame_geo.size(), size_mode::fixed_height));

    // May cause leave event.
    win->space.focusMousePos = input::get_cursor()->pos();
    win->setFrameGeometry(frame_geo);
}

template<typename Win>
void shrink_vertical(Win* win)
{
    if (!win->isResizable()) {
        return;
    }

    auto frame_geo = win->frameGeometry();
    frame_geo.setBottom(win->space.packPositionUp(win, frame_geo.bottom(), false));
    if (frame_geo.height() <= 1) {
        return;
    }

    frame_geo.setSize(adjusted_frame_size(win, frame_geo.size(), size_mode::fixed_height));

    // TODO(romangg): Magic number 20. Why?
    if (frame_geo.height() > 20) {
        // May cause leave event.
        win->space.focusMousePos = input::get_cursor()->pos();
        win->setFrameGeometry(frame_geo);
    }
}

}
