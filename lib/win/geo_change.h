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

template<typename Space, typename Win>
int get_pack_position_left(Space const& space, Win const* window, int oldX, bool leftEdge)
{
    int newX = space_window_area(space, area_option::maximize, window).left();
    if (oldX <= newX) {
        // try another Xinerama screen
        newX = space_window_area(space,
                                 area_option::maximize,
                                 QPoint(window->geo.update.frame.left() - 1,
                                        window->geo.update.frame.center().y()),
                                 get_subspace(*window))
                   .left();
    }

    auto const right = newX - win::frame_margins(window).left();
    auto frameGeometry = window->geo.update.frame;
    frameGeometry.moveRight(right);
    if (base::get_intersecting_outputs(space.base.outputs, frameGeometry).size() < 2) {
        newX = right;
    }

    if (oldX <= newX) {
        return oldX;
    }

    const int subspace = get_subspace(*window) == 0 || on_all_subspaces(*window)
        ? space.subspace_manager->current()
        : get_subspace(*window);

    for (auto win : space.windows) {
        std::visit(
            overload{[&](auto&& win) {
                if (is_irrelevant(win, window, subspace)) {
                    return;
                }
                int const x = leftEdge ? win->geo.update.frame.right() + 1
                                       : win->geo.update.frame.left() - 1;
                if (x > newX && x < oldX
                    && !(window->geo.update.frame.top() > win->geo.update.frame.bottom()
                         || window->geo.update.frame.bottom() < win->geo.update.frame.top())) {
                    newX = x;
                }
            }},
            win);
    }
    return newX;
}

template<typename Space, typename Win>
int get_pack_position_right(Space const& space, Win const* window, int oldX, bool rightEdge)
{
    int newX = space_window_area(space, area_option::maximize, window).right();

    if (oldX >= newX) {
        // try another Xinerama screen
        newX = space_window_area(space,
                                 area_option::maximize,
                                 QPoint(window->geo.update.frame.right() + 1,
                                        window->geo.update.frame.center().y()),
                                 get_subspace(*window))
                   .right();
    }

    auto const right = newX + win::frame_margins(window).right();
    auto frameGeometry = window->geo.update.frame;
    frameGeometry.moveRight(right);
    if (base::get_intersecting_outputs(space.base.outputs, frameGeometry).size() < 2) {
        newX = right;
    }

    if (oldX >= newX) {
        return oldX;
    }

    int const subspace = get_subspace(*window) == 0 || on_all_subspaces(*window)
        ? space.subspace_manager->current()
        : get_subspace(*window);

    for (auto win : space.windows) {
        std::visit(
            overload{[&](auto&& win) {
                if (is_irrelevant(win, window, subspace)) {
                    return;
                }
                int const x = rightEdge ? win->geo.update.frame.left() - 1
                                        : win->geo.update.frame.right() + 1;
                if (x < newX && x > oldX
                    && !(window->geo.update.frame.top() > win->geo.update.frame.bottom()
                         || window->geo.update.frame.bottom() < win->geo.update.frame.top())) {
                    newX = x;
                }
            }},
            win);
    }
    return newX;
}

template<typename Space, typename Win>
int get_pack_position_up(Space const& space, Win const* window, int oldY, bool topEdge)
{
    int newY = space_window_area(space, area_option::maximize, window).top();
    if (oldY <= newY) {
        // try another Xinerama screen
        newY = space_window_area(space,
                                 area_option::maximize,
                                 QPoint(window->geo.update.frame.center().x(),
                                        window->geo.update.frame.top() - 1),
                                 get_subspace(*window))
                   .top();
    }

    if (oldY <= newY) {
        return oldY;
    }

    int const subspace = get_subspace(*window) == 0 || on_all_subspaces(*window)
        ? space.subspace_manager->current()
        : get_subspace(*window);

    for (auto win : space.windows) {
        std::visit(
            overload{[&](auto&& win) {
                if (is_irrelevant(win, window, subspace)) {
                    return;
                }
                int const y = topEdge ? win->geo.update.frame.bottom() + 1
                                      : win->geo.update.frame.top() - 1;
                if (y > newY && y < oldY
                    && !(window->geo.update.frame.left()
                             > win->geo.update.frame.right() // they overlap in X direction
                         || window->geo.update.frame.right() < win->geo.update.frame.left())) {
                    newY = y;
                }
            }},
            win);
    }
    return newY;
}

template<typename Space, typename Win>
int get_pack_position_down(Space const& space, Win const* window, int oldY, bool bottomEdge)
{
    int newY = space_window_area(space, area_option::maximize, window).bottom();
    if (oldY >= newY) { // try another Xinerama screen
        newY = space_window_area(space,
                                 area_option::maximize,
                                 QPoint(window->geo.update.frame.center().x(),
                                        window->geo.update.frame.bottom() + 1),
                                 get_subspace(*window))
                   .bottom();
    }

    auto const bottom = newY + win::frame_margins(window).bottom();
    auto frameGeometry = window->geo.update.frame;
    frameGeometry.moveBottom(bottom);
    if (base::get_intersecting_outputs(space.base.outputs, frameGeometry).size() < 2) {
        newY = bottom;
    }

    if (oldY >= newY) {
        return oldY;
    }

    int const subspace = get_subspace(*window) == 0 || on_all_subspaces(*window)
        ? space.subspace_manager->current()
        : get_subspace(*window);

    for (auto win : space.windows) {
        std::visit(
            overload{[&](auto&& win) {
                if (is_irrelevant(win, window, subspace)) {
                    return;
                }
                int const y = bottomEdge ? win->geo.update.frame.top() - 1
                                         : win->geo.update.frame.bottom() + 1;
                if (y < newY && y > oldY
                    && !(window->geo.update.frame.left() > win->geo.update.frame.right()
                         || window->geo.update.frame.right() < win->geo.update.frame.left())) {
                    newY = y;
                }
            }},
            win);
    }
    return newY;
}

template<typename Win>
QSize constrain_and_adjust_size(Win* win, QSize const& size)
{
    auto width = size.width();
    auto height = size.height();

    auto const area = space_window_area(win->space, area_option::work, win);

    width = std::min(width, area.width());
    height = std::min(height, area.height());

    // checks size constraints, including min/max size
    return adjusted_frame_size(win, QSize(width, height), win::size_mode::any);
}

template<typename Win>
void constrained_resize(Win* win, QSize const& size)
{
    win->setFrameGeometry(
        QRect(win->geo.update.frame.topLeft(), constrain_and_adjust_size(win, size)));
}

template<typename Win>
void grow_horizontal(Win* win)
{
    if (!win->isResizable()) {
        return;
    }

    auto frame_geo = win->geo.frame;
    frame_geo.setRight(get_pack_position_right(win->space, win, frame_geo.right(), true));
    auto const adjsize = adjusted_frame_size(win, frame_geo.size(), size_mode::fixed_width);

    if (win->geo.size() == adjsize && frame_geo.size() != adjsize
        && win->resizeIncrements().width() > 1) {
        // Grow by increment.
        auto const grown_right = get_pack_position_right(
            win->space, win, frame_geo.right() + win->resizeIncrements().width() - 1, true);

        // Check that it hasn't grown outside of the area, due to size increments.
        // TODO this may be wrong?
        auto const area = space_window_area(
            win->space,
            area_option::movement,
            QPoint((win->geo.pos().x() + grown_right) / 2, win->geo.frame.center().y()),
            get_subspace(*win));
        if (area.right() >= grown_right) {
            frame_geo.setRight(grown_right);
        }
    }

    frame_geo.setSize(adjusted_frame_size(win, frame_geo.size(), size_mode::fixed_width));
    frame_geo.setSize(adjusted_frame_size(win, frame_geo.size(), size_mode::fixed_height));

    // May cause leave event.
    win->space.focusMousePos = win->space.input->cursor->pos();
    win->setFrameGeometry(frame_geo);
}

template<typename Win>
void shrink_horizontal(Win* win)
{
    if (!win->isResizable()) {
        return;
    }

    auto geom = win->geo.frame;
    geom.setRight(get_pack_position_left(win->space, win, geom.right(), false));

    if (geom.width() <= 1) {
        return;
    }

    geom.setSize(adjusted_frame_size(win, geom.size(), size_mode::fixed_width));

    // TODO(romangg): Magic number 20. Why?
    if (geom.width() > 20) {
        // May cause leave event.
        win->space.focusMousePos = win->space.input->cursor->pos();
        win->setFrameGeometry(geom);
    }
}

template<typename Win>
void grow_vertical(Win* win)
{
    if (!win->isResizable()) {
        return;
    }

    auto frame_geo = win->geo.frame;
    frame_geo.setBottom(get_pack_position_down(win->space, win, frame_geo.bottom(), true));
    auto adjsize = adjusted_frame_size(win, frame_geo.size(), size_mode::fixed_height);

    if (win->geo.size() == adjsize && frame_geo.size() != adjsize
        && win->resizeIncrements().height() > 1) {
        // Grow by increment.
        auto const newbottom = get_pack_position_down(
            win->space, win, frame_geo.bottom() + win->resizeIncrements().height() - 1, true);

        // check that it hasn't grown outside of the area, due to size increments
        auto const area = space_window_area(
            win->space,
            area_option::movement,
            QPoint(win->geo.frame.center().x(), (win->geo.pos().y() + newbottom) / 2),
            get_subspace(*win));
        if (area.bottom() >= newbottom) {
            frame_geo.setBottom(newbottom);
        }
    }

    frame_geo.setSize(adjusted_frame_size(win, frame_geo.size(), size_mode::fixed_height));

    // May cause leave event.
    win->space.focusMousePos = win->space.input->cursor->pos();
    win->setFrameGeometry(frame_geo);
}

template<typename Win>
void shrink_vertical(Win* win)
{
    if (!win->isResizable()) {
        return;
    }

    auto frame_geo = win->geo.frame;
    frame_geo.setBottom(get_pack_position_up(win->space, win, frame_geo.bottom(), false));
    if (frame_geo.height() <= 1) {
        return;
    }

    frame_geo.setSize(adjusted_frame_size(win, frame_geo.size(), size_mode::fixed_height));

    // TODO(romangg): Magic number 20. Why?
    if (frame_geo.height() > 20) {
        // May cause leave event.
        win->space.focusMousePos = win->space.input->cursor->pos();
        win->setFrameGeometry(frame_geo);
    }
}

}
