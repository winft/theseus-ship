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

template<typename Space>
int get_pack_position_left(Space const& space, Toplevel const* window, int oldX, bool leftEdge)
{
    int newX = space_window_area(space, MaximizeArea, window).left();
    if (oldX <= newX) {
        // try another Xinerama screen
        newX = space_window_area(space,
                                 MaximizeArea,
                                 QPoint(window->geometry_update.frame.left() - 1,
                                        window->geometry_update.frame.center().y()),
                                 window->desktop())
                   .left();
    }

    auto const right = newX - win::frame_margins(window).left();
    auto frameGeometry = window->geometry_update.frame;
    frameGeometry.moveRight(right);
    if (base::get_intersecting_outputs(kwinApp()->get_base().get_outputs(), frameGeometry).size()
        < 2) {
        newX = right;
    }

    if (oldX <= newX) {
        return oldX;
    }

    const int desktop = window->desktop() == 0 || window->isOnAllDesktops()
        ? space.virtual_desktop_manager->current()
        : window->desktop();
    for (auto win : space.windows) {
        if (is_irrelevant(win, window, desktop)) {
            continue;
        }
        const int x = leftEdge ? win->geometry_update.frame.right() + 1
                               : win->geometry_update.frame.left() - 1;
        if (x > newX && x < oldX
            && !(window->geometry_update.frame.top() > win->geometry_update.frame.bottom()
                 || window->geometry_update.frame.bottom() < win->geometry_update.frame.top())) {
            newX = x;
        }
    }
    return newX;
}

template<typename Space>
int get_pack_position_right(Space const& space, Toplevel const* window, int oldX, bool rightEdge)
{
    int newX = space_window_area(space, MaximizeArea, window).right();

    if (oldX >= newX) {
        // try another Xinerama screen
        newX = space_window_area(space,
                                 MaximizeArea,
                                 QPoint(window->geometry_update.frame.right() + 1,
                                        window->geometry_update.frame.center().y()),
                                 window->desktop())
                   .right();
    }

    auto const right = newX + win::frame_margins(window).right();
    auto frameGeometry = window->geometry_update.frame;
    frameGeometry.moveRight(right);
    if (base::get_intersecting_outputs(kwinApp()->get_base().get_outputs(), frameGeometry).size()
        < 2) {
        newX = right;
    }

    if (oldX >= newX) {
        return oldX;
    }

    int const desktop = window->desktop() == 0 || window->isOnAllDesktops()
        ? space.virtual_desktop_manager->current()
        : window->desktop();
    for (auto win : space.windows) {
        if (is_irrelevant(win, window, desktop)) {
            continue;
        }
        const int x = rightEdge ? win->geometry_update.frame.left() - 1
                                : win->geometry_update.frame.right() + 1;
        if (x < newX && x > oldX
            && !(window->geometry_update.frame.top() > win->geometry_update.frame.bottom()
                 || window->geometry_update.frame.bottom() < win->geometry_update.frame.top())) {
            newX = x;
        }
    }
    return newX;
}

template<typename Space>
int get_pack_position_up(Space const& space, Toplevel const* window, int oldY, bool topEdge)
{
    int newY = space_window_area(space, MaximizeArea, window).top();
    if (oldY <= newY) {
        // try another Xinerama screen
        newY = space_window_area(space,
                                 MaximizeArea,
                                 QPoint(window->geometry_update.frame.center().x(),
                                        window->geometry_update.frame.top() - 1),
                                 window->desktop())
                   .top();
    }

    if (oldY <= newY) {
        return oldY;
    }

    int const desktop = window->desktop() == 0 || window->isOnAllDesktops()
        ? space.virtual_desktop_manager->current()
        : window->desktop();
    for (auto win : space.windows) {
        if (is_irrelevant(win, window, desktop)) {
            continue;
        }
        const int y = topEdge ? win->geometry_update.frame.bottom() + 1
                              : win->geometry_update.frame.top() - 1;
        if (y > newY && y < oldY
            && !(window->geometry_update.frame.left()
                     > win->geometry_update.frame.right() // they overlap in X direction
                 || window->geometry_update.frame.right() < win->geometry_update.frame.left())) {
            newY = y;
        }
    }
    return newY;
}

template<typename Space>
int get_pack_position_down(Space const& space, Toplevel const* window, int oldY, bool bottomEdge)
{
    int newY = space_window_area(space, MaximizeArea, window).bottom();
    if (oldY >= newY) { // try another Xinerama screen
        newY = space_window_area(space,
                                 MaximizeArea,
                                 QPoint(window->geometry_update.frame.center().x(),
                                        window->geometry_update.frame.bottom() + 1),
                                 window->desktop())
                   .bottom();
    }

    auto const bottom = newY + win::frame_margins(window).bottom();
    auto frameGeometry = window->geometry_update.frame;
    frameGeometry.moveBottom(bottom);
    if (base::get_intersecting_outputs(kwinApp()->get_base().get_outputs(), frameGeometry).size()
        < 2) {
        newY = bottom;
    }

    if (oldY >= newY) {
        return oldY;
    }
    int const desktop = window->desktop() == 0 || window->isOnAllDesktops()
        ? space.virtual_desktop_manager->current()
        : window->desktop();
    for (auto win : space.windows) {
        if (is_irrelevant(win, window, desktop)) {
            continue;
        }
        const int y = bottomEdge ? win->geometry_update.frame.top() - 1
                                 : win->geometry_update.frame.bottom() + 1;
        if (y < newY && y > oldY
            && !(window->geometry_update.frame.left() > win->geometry_update.frame.right()
                 || window->geometry_update.frame.right() < win->geometry_update.frame.left())) {
            newY = y;
        }
    }
    return newY;
}

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
    frame_geo.setRight(get_pack_position_right(win->space, win, frame_geo.right(), true));
    auto const adjsize = adjusted_frame_size(win, frame_geo.size(), size_mode::fixed_width);

    if (win->frameGeometry().size() == adjsize && frame_geo.size() != adjsize
        && win->resizeIncrements().width() > 1) {
        // Grow by increment.
        auto const grown_right = get_pack_position_right(
            win->space, win, frame_geo.right() + win->resizeIncrements().width() - 1, true);

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
    geom.setRight(get_pack_position_left(win->space, win, geom.right(), false));

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
    frame_geo.setBottom(get_pack_position_down(win->space, win, frame_geo.bottom(), true));
    auto adjsize = adjusted_frame_size(win, frame_geo.size(), size_mode::fixed_height);

    if (win->frameGeometry().size() == adjsize && frame_geo.size() != adjsize
        && win->resizeIncrements().height() > 1) {
        // Grow by increment.
        auto const newbottom = get_pack_position_down(
            win->space, win, frame_geo.bottom() + win->resizeIncrements().height() - 1, true);

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
    frame_geo.setBottom(get_pack_position_up(win->space, win, frame_geo.bottom(), false));
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
