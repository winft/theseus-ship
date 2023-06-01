/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/deco.h"

#include <Wrapland/Server/xdg_shell_surface.h>
#include <functional>

namespace KWin::win::wayland
{

template<typename Win>
struct popup_placement_data {
    Win* parent_window;
    QRect const& bounds;
    QRect const& anchor_rect;
    Qt::Edges const& anchor_edges;
    Qt::Edges const& gravity;
    QSize const& size;
    QPoint const& offset;
    Wrapland::Server::XdgShellSurface::ConstraintAdjustments adjustments;
};

/// Returns true if target is within bounds. The edges argument states which sides to check.
inline bool check_bounds(QRect const& target, QRect const& bounds, Qt::Edges edges)
{
    if (edges & Qt::LeftEdge && target.left() < bounds.left()) {
        return false;
    }
    if (edges & Qt::TopEdge && target.top() < bounds.top()) {
        return false;
    }
    if (edges & Qt::RightEdge && target.right() > bounds.right()) {
        return false;
    }
    if (edges & Qt::BottomEdge && target.bottom() > bounds.bottom()) {
        return false;
    }
    return true;
}

inline bool check_all_bounds(QRect const& target, QRect const& bounds)
{
    auto all_bounds = Qt::LeftEdge | Qt::RightEdge | Qt::TopEdge | Qt::BottomEdge;
    return check_bounds(target, bounds, all_bounds);
}

/**
 * Calculate where the top left point of the popup will end up with the applied gravity.
 * Gravity indicates direction, i.e, if gravitating towards the top the popup's
 * bottom edge will be next to the anchor point.
 */
inline QPoint get_anchor(QRect const& rect, Qt::Edges edge, Qt::Edges gravity, QSize const& size)
{
    QPoint pos;

    switch (edge & (Qt::LeftEdge | Qt::RightEdge)) {
    case Qt::LeftEdge:
        pos.setX(rect.x());
        break;
    case Qt::RightEdge:
        pos.setX(rect.x() + rect.width());
        break;
    default:
        pos.setX(qRound(rect.x() + rect.width() / 2.0));
    }

    switch (edge & (Qt::TopEdge | Qt::BottomEdge)) {
    case Qt::TopEdge:
        pos.setY(rect.y());
        break;
    case Qt::BottomEdge:
        pos.setY(rect.y() + rect.height());
        break;
    default:
        pos.setY(qRound(rect.y() + rect.height() / 2.0));
    }

    QPoint pos_adjust;
    switch (gravity & (Qt::LeftEdge | Qt::RightEdge)) {
    case Qt::LeftEdge:
        pos_adjust.setX(-size.width());
        break;
    case Qt::RightEdge:
        pos_adjust.setX(0);
        break;
    default:
        pos_adjust.setX(qRound(-size.width() / 2.0));
    }
    switch (gravity & (Qt::TopEdge | Qt::BottomEdge)) {
    case Qt::TopEdge:
        pos_adjust.setY(-size.height());
        break;
    case Qt::BottomEdge:
        pos_adjust.setY(0);
        break;
    default:
        pos_adjust.setY(qRound(-size.height() / 2.0));
    }

    return pos + pos_adjust;
}

template<typename Win>
void adjust_by_flip_slide_resize(QRect& place, popup_placement_data<Win> const& data)
{
    auto const parent_pos = data.parent_window->geo.pos()
        + QPoint(win::left_border(data.parent_window), win::top_border(data.parent_window));

    auto in_bounds
        = std::bind(check_bounds, std::placeholders::_1, data.bounds, std::placeholders::_2);
    using ConstraintAdjustment = Wrapland::Server::XdgShellSurface::ConstraintAdjustment;

    if (data.adjustments & ConstraintAdjustment::FlipX) {
        if (!in_bounds(place, Qt::LeftEdge | Qt::RightEdge)) {
            // Flip both edges (if either bit is set, XOR both).
            auto flippedanchor_edge = data.anchor_edges;
            if (flippedanchor_edge & (Qt::LeftEdge | Qt::RightEdge)) {
                flippedanchor_edge ^= (Qt::LeftEdge | Qt::RightEdge);
            }
            auto flippedGravity = data.gravity;
            if (flippedGravity & (Qt::LeftEdge | Qt::RightEdge)) {
                flippedGravity ^= (Qt::LeftEdge | Qt::RightEdge);
            }
            auto flipped_place
                = QRect(get_anchor(data.anchor_rect, flippedanchor_edge, flippedGravity, data.size)
                            + data.offset + parent_pos,
                        data.size);

            // If it still doesn't fit continue with the unflipped version.
            if (in_bounds(flipped_place, Qt::LeftEdge | Qt::RightEdge)) {
                place.moveLeft(flipped_place.left());
            }
        }
    }
    if (data.adjustments & ConstraintAdjustment::SlideX) {
        if (!in_bounds(place, Qt::LeftEdge)) {
            place.moveLeft(data.bounds.left());
        }
        if (!in_bounds(place, Qt::RightEdge)) {
            place.moveRight(data.bounds.right());
        }
    }
    if (data.adjustments & ConstraintAdjustment::ResizeX) {
        auto unconstrained_place = place;

        if (!in_bounds(unconstrained_place, Qt::LeftEdge)) {
            unconstrained_place.setLeft(data.bounds.left());
        }
        if (!in_bounds(unconstrained_place, Qt::RightEdge)) {
            unconstrained_place.setRight(data.bounds.right());
        }

        if (unconstrained_place.isValid()) {
            place = unconstrained_place;
        }
    }

    if (data.adjustments & ConstraintAdjustment::FlipY) {
        if (!in_bounds(place, Qt::TopEdge | Qt::BottomEdge)) {
            // flip both edges (if either bit is set, XOR both)
            auto flippedanchor_edge = data.anchor_edges;
            if (flippedanchor_edge & (Qt::TopEdge | Qt::BottomEdge)) {
                flippedanchor_edge ^= (Qt::TopEdge | Qt::BottomEdge);
            }
            auto flippedGravity = data.gravity;
            if (flippedGravity & (Qt::TopEdge | Qt::BottomEdge)) {
                flippedGravity ^= (Qt::TopEdge | Qt::BottomEdge);
            }
            auto flipped_place
                = QRect(get_anchor(data.anchor_rect, flippedanchor_edge, flippedGravity, data.size)
                            + data.offset + parent_pos,
                        data.size);

            // if it still doesn't fit we should continue with the unflipped version
            if (in_bounds(flipped_place, Qt::TopEdge | Qt::BottomEdge)) {
                place.moveTop(flipped_place.top());
            }
        }
    }
    if (data.adjustments & ConstraintAdjustment::SlideY) {
        if (!in_bounds(place, Qt::TopEdge)) {
            place.moveTop(data.bounds.top());
        }
        if (!in_bounds(place, Qt::BottomEdge)) {
            place.moveBottom(data.bounds.bottom());
        }
    }
    if (data.adjustments & ConstraintAdjustment::ResizeY) {
        auto unconstrained_place = place;

        if (!in_bounds(unconstrained_place, Qt::TopEdge)) {
            unconstrained_place.setTop(data.bounds.top());
        }
        if (!in_bounds(unconstrained_place, Qt::BottomEdge)) {
            unconstrained_place.setBottom(data.bounds.bottom());
        }

        if (unconstrained_place.isValid()) {
            place = unconstrained_place;
        }
    }
}

template<typename Win>
QRect get_popup_placement(popup_placement_data<Win> const& data)
{
    auto const parent_pos = data.parent_window->geo.pos()
        + QPoint(win::left_border(data.parent_window), win::top_border(data.parent_window));

    auto placement_pos = get_anchor(data.anchor_rect, data.anchor_edges, data.gravity, data.size)
        + data.offset + parent_pos;
    auto place = QRect(placement_pos, data.size);

    if (!win::wayland::check_all_bounds(place, data.bounds)) {
        win::wayland::adjust_by_flip_slide_resize(place, data);
    }

    return place;
}

}
