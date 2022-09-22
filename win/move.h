/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "deco.h"
#include "geo_block.h"
#include "geo_change.h"
#include "geo_electric.h"
#include "geo_move.h"
#include "net.h"
#include "quicktile.h"
#include "scene.h"
#include "stacking.h"
#include "types.h"
#include "window_area.h"

#include "base/output_helpers.h"
#include "base/platform.h"
#include "input/cursor.h"
#include "render/outline.h"

#include <QWidget>

namespace KWin::win
{

inline int sign(int v)
{
    return (v > 0) - (v < 0);
}

/**
 * Position of pointer depending on decoration section the pointer is above.
 * Without decorations or when pointer is not above a decoration position center is returned.
 */
template<typename Win>
position mouse_position(Win* win)
{
    auto deco = decoration(win);
    if (!deco) {
        return position::center;
    }

    switch (deco->sectionUnderMouse()) {
    case Qt::BottomLeftSection:
        return position::bottom_left;
    case Qt::BottomRightSection:
        return position::bottom_right;
    case Qt::BottomSection:
        return position::bottom;
    case Qt::LeftSection:
        return position::left;
    case Qt::RightSection:
        return position::right;
    case Qt::TopSection:
        return position::top;
    case Qt::TopLeftSection:
        return position::top_left;
    case Qt::TopRightSection:
        return position::top_right;
    default:
        return position::center;
    }
}

template<typename Space>
void set_move_resize_window(Space& space, typename Space::window_t* window)
{
    // Catch attempts to move a second
    assert(!window || !space.move_resize_window);

    // window while still moving the first one.
    space.move_resize_window = window;

    if (space.move_resize_window) {
        ++space.block_focus;
    } else {
        --space.block_focus;
    }
}

template<typename Win>
void update_cursor(Win* win)
{
    auto& mov_res = win->control->move_resize;
    auto contact = mov_res.contact;

    if (!win->isResizable()) {
        contact = win::position::center;
    }
    input::cursor_shape shape = Qt::ArrowCursor;
    switch (contact) {
    case win::position::top_left:
        shape = KWin::input::extended_cursor::SizeNorthWest;
        break;
    case win::position::bottom_right:
        shape = KWin::input::extended_cursor::SizeSouthEast;
        break;
    case win::position::bottom_left:
        shape = KWin::input::extended_cursor::SizeSouthWest;
        break;
    case win::position::top_right:
        shape = KWin::input::extended_cursor::SizeNorthEast;
        break;
    case win::position::top:
        shape = KWin::input::extended_cursor::SizeNorth;
        break;
    case win::position::bottom:
        shape = KWin::input::extended_cursor::SizeSouth;
        break;
    case win::position::left:
        shape = KWin::input::extended_cursor::SizeWest;
        break;
    case win::position::right:
        shape = KWin::input::extended_cursor::SizeEast;
        break;
    default:
        if (mov_res.enabled) {
            shape = Qt::SizeAllCursor;
        } else {
            shape = Qt::ArrowCursor;
        }
        break;
    }
    if (shape == mov_res.cursor) {
        return;
    }
    mov_res.cursor = shape;
    Q_EMIT win->qobject->moveResizeCursorChanged(shape);
}

/**
 * Returns @c true if @p win is being interactively resized; otherwise @c false.
 */
template<typename Win>
bool is_resize(Win* win)
{
    auto const& mov_res = win->control->move_resize;
    return mov_res.enabled && mov_res.contact != position::center;
}

// This function checks if it actually makes sense to perform a restricted move/resize.
// If e.g. the titlebar is already outside of the workarea, there's no point in performing
// a restricted move resize, because then e.g. resize would also move the window (#74555).
template<typename Win>
void check_unrestricted_move_resize(Win* win)
{
    auto& mov_res = win->control->move_resize;
    if (mov_res.unrestricted) {
        return;
    }

    auto desktopArea
        = space_window_area(win->space, WorkArea, mov_res.geometry.center(), get_desktop(*win));
    int left_marge, right_marge, top_marge, bottom_marge, titlebar_marge;

    // restricted move/resize - keep at least part of the titlebar always visible
    // how much must remain visible when moved away in that direction
    left_marge = std::min(100 + right_border(win), mov_res.geometry.width());
    right_marge = std::min(100 + left_border(win), mov_res.geometry.width());

    // width/height change with opaque resizing, use the initial ones
    titlebar_marge = mov_res.initial_geometry.height();
    top_marge = bottom_border(win);
    bottom_marge = top_border(win);

    auto has_unrestricted_resize = [&] {
        if (!is_resize(win)) {
            return false;
        }
        if (mov_res.geometry.bottom() < desktopArea.top() + top_marge) {
            return true;
        }
        if (mov_res.geometry.top() > desktopArea.bottom() - bottom_marge) {
            return true;
        }
        if (mov_res.geometry.right() < desktopArea.left() + left_marge) {
            return true;
        }
        if (mov_res.geometry.left() > desktopArea.right() - right_marge) {
            return true;
        }
        if (!mov_res.unrestricted && mov_res.geometry.top() < desktopArea.top()) {
            return true;
        }
        return false;
    };

    if (has_unrestricted_resize()) {
        mov_res.unrestricted = true;
    }

    auto has_unrestricted_move = [&] {
        if (!is_move(win)) {
            return false;
        }
        if (mov_res.geometry.bottom() < desktopArea.top() + titlebar_marge - 1) {
            return true;
        }

        // No need to check top_marge, titlebar_marge already handles it
        if (mov_res.geometry.top() > desktopArea.bottom() - bottom_marge + 1) {
            return true;
        }
        if (mov_res.geometry.right() < desktopArea.left() + left_marge) {
            return true;
        }
        if (mov_res.geometry.left() > desktopArea.right() - right_marge) {
            return true;
        }
        return false;
    };

    if (has_unrestricted_move()) {
        mov_res.unrestricted = true;
    }
}

template<typename Win>
void maximize(Win* win, maximize_mode mode)
{
    win->update_maximized(mode);
}

template<typename Win>
void stop_delayed_move_resize(Win* win)
{
    auto& mov_res = win->control->move_resize;
    delete mov_res.delay_timer;
    mov_res.delay_timer = nullptr;
}

template<typename Win>
bool start_move_resize(Win* win)
{
    auto& mov_res = win->control->move_resize;

    assert(!mov_res.enabled);
    assert(QWidget::keyboardGrabber() == nullptr);
    assert(QWidget::mouseGrabber() == nullptr);

    stop_delayed_move_resize(win);

    if (QApplication::activePopupWidget()) {
        // Popups have grab.
        return false;
    }
    if (win->control->fullscreen
        && (win->space.base.outputs.size() < 2 || !win->isMovableAcrossScreens())) {
        return false;
    }
    if (!win->doStartMoveResize()) {
        return false;
    }

    win->control->deco.double_click.stop();

    auto const mode = mov_res.contact;
    auto const was_maxed_full = win->maximizeMode() == maximize_mode::full;
    auto const was_tiled = win->control->quicktiling != quicktiles::none;
    auto const was_fullscreen = win->geo.update.fullscreen;

    if (mode == position::center) {
        // That's a move.
        // TODO(romangg): Shorten the following condition to just restore geometry being invalid?
        if (!was_maxed_full && !was_tiled && !was_fullscreen) {
            // Remember current geometry in case the window is later moved to an edge for tiling.
            win->geo.restore.max = win->geo.frame;
        }
    } else {
        // That's a resize.
        win->geo.restore.max = win->geo.frame;
        if (was_maxed_full) {
            set_maximize(win, false, false);
        }
        if (win->control->quicktiling != quicktiles::none) {
            // Exit quick tile mode when the user attempts to resize a tiled window.
            set_quicktile_mode(win, quicktiles::none, false);
        }
        win->geo.restore.max = QRect();
    }

    mov_res.enabled = true;
    set_move_resize_window(win->space, win);

    win->control->update_have_resize_effect();

    mov_res.initial_geometry = pending_frame_geometry(win);
    mov_res.geometry = mov_res.initial_geometry;
    mov_res.start_screen = win->topo.central_output
        ? base::get_output_index(win->space.base.outputs, *win->topo.central_output)
        : 0;

    check_unrestricted_move_resize(win);

    Q_EMIT win->qobject->clientStartUserMovedResized();

    if (win->space.edges->desktop_switching.when_moving_client) {
        win->space.edges->reserveDesktopSwitching(true, Qt::Vertical | Qt::Horizontal);
    }

    return true;
}

template<typename Win>
void perform_move_resize(Win* win)
{
    auto const& geom = win->control->move_resize.geometry;

    if (is_move(win)) {
        win->setFrameGeometry(geom);
    }

    win->doPerformMoveResize();
    Q_EMIT win->qobject->clientStepUserMovedResized(geom);
}

template<typename Win>
auto move_resize_impl(Win* win, int x, int y, int x_root, int y_root)
{
    if (win->isWaitingForMoveResizeSync()) {
        return;
    }

    auto& mov_res = win->control->move_resize;

    auto const mode = mov_res.contact;
    if ((mode == position::center && !win->isMovableAcrossScreens())
        || (mode != position::center && !win->isResizable())) {
        return;
    }

    if (!mov_res.enabled) {
        auto p = QPoint(x, y) - mov_res.offset;
        if (p.manhattanLength() >= QApplication::startDragDistance()) {
            if (!start_move_resize(win)) {
                mov_res.button_down = false;
                update_cursor(win);
                return;
            }
            update_cursor(win);
        } else {
            return;
        }
    }

    QPoint globalPos(x_root, y_root);
    // these two points limit the geometry rectangle, i.e. if bottomleft resizing is done,
    // the bottomleft corner should be at is at (topleft.x(), bottomright().y())
    auto topleft = globalPos - mov_res.offset;
    auto bottomright = globalPos + mov_res.inverted_offset;
    auto previousMoveResizeGeom = mov_res.geometry;

    // TODO move whole group when moving its leader or when the leader is not mapped?

    auto titleBarRect = [&win](bool& transposed, int& requiredPixels) -> QRect {
        auto const& moveResizeGeom = win->control->move_resize.geometry;
        QRect r(moveResizeGeom);
        r.moveTopLeft(QPoint(0, 0));
        r.setHeight(top_border(win));
        // When doing a restricted move we must always keep 100px of the titlebar
        // visible to allow the user to be able to move it again.
        requiredPixels = std::min(100 * (transposed ? r.width() : r.height()),
                                  moveResizeGeom.width() * moveResizeGeom.height());
        return r;
    };

    bool update = false;

    if (is_resize(win)) {
        auto orig = mov_res.initial_geometry;
        auto sizeMode = size_mode::any;

        auto calculateMoveResizeGeom = [&win, &topleft, &bottomright, &orig, &sizeMode, &mode]() {
            auto& mov_res = win->control->move_resize;
            switch (mode) {
            case position::top_left:
                mov_res.geometry = QRect(topleft, orig.bottomRight());
                break;
            case position::bottom_right:
                mov_res.geometry = QRect(orig.topLeft(), bottomright);
                break;
            case position::bottom_left:
                mov_res.geometry
                    = QRect(QPoint(topleft.x(), orig.y()), QPoint(orig.right(), bottomright.y()));
                break;
            case position::top_right:
                mov_res.geometry
                    = QRect(QPoint(orig.x(), topleft.y()), QPoint(bottomright.x(), orig.bottom()));
                break;
            case position::top:
                mov_res.geometry = QRect(QPoint(orig.left(), topleft.y()), orig.bottomRight());
                sizeMode = size_mode::fixed_height; // try not to affect height
                break;
            case position::bottom:
                mov_res.geometry = QRect(orig.topLeft(), QPoint(orig.right(), bottomright.y()));
                sizeMode = size_mode::fixed_height;
                break;
            case position::left:
                mov_res.geometry = QRect(QPoint(topleft.x(), orig.top()), orig.bottomRight());
                sizeMode = size_mode::fixed_width;
                break;
            case position::right:
                mov_res.geometry = QRect(orig.topLeft(), QPoint(bottomright.x(), orig.bottom()));
                sizeMode = size_mode::fixed_width;
                break;
            case position::center:
            default:
                abort();
                break;
            }
        };

        // first resize (without checking constrains), then snap, then check bounds, then check
        // constrains
        calculateMoveResizeGeom();

        // adjust new size to snap to other windows/borders
        mov_res.geometry = adjust_window_size(win->space, *win, mov_res.geometry, mode);

        if (!mov_res.unrestricted) {
            // Make sure the titlebar isn't behind a restricted area. We don't need to restrict
            // the other directions. If not visible enough, move the window to the closest valid
            // point. We bruteforce this by slowly moving the window back to its previous position

            // On the screen
            QRegion availableArea(space_window_area(win->space, FullArea, nullptr, 0));

            // Strut areas
            availableArea -= restricted_move_area(win->space, get_desktop(*win), strut_area::all);

            bool transposed = false;
            int requiredPixels;
            QRect bTitleRect = titleBarRect(transposed, requiredPixels);
            int lastVisiblePixels = -1;
            auto lastTry = mov_res.geometry;
            bool titleFailed = false;

            for (;;) {
                auto const titleRect = bTitleRect.translated(mov_res.geometry.topLeft());
                int visiblePixels = 0;
                int realVisiblePixels = 0;

                for (auto const& area : availableArea) {
                    auto const r = area & titleRect;
                    realVisiblePixels += r.width() * r.height();

                    // Only full size regions and prevent long slim areas.
                    if ((transposed && r.width() == titleRect.width())
                        || (!transposed && r.height() == titleRect.height())) {
                        visiblePixels += r.width() * r.height();
                    }
                }

                if (visiblePixels >= requiredPixels)
                    break; // We have reached a valid position

                if (realVisiblePixels <= lastVisiblePixels) {
                    if (titleFailed && realVisiblePixels < lastVisiblePixels)
                        break; // we won't become better
                    else {
                        if (!titleFailed) {
                            mov_res.geometry = lastTry;
                        }
                        titleFailed = true;
                    }
                }
                lastVisiblePixels = realVisiblePixels;
                auto moveResizeGeom = mov_res.geometry;
                lastTry = moveResizeGeom;

                // Not visible enough, move the window to the closest valid point. We bruteforce
                // this by slowly moving the window back to its previous position.
                // The geometry changes at up to two edges, the one with the title (if) shall take
                // precedence. The opposing edge has no impact on visiblePixels and only one of
                // the adjacent can alter at a time, ie. it's enough to ignore adjacent edges
                // if the title edge altered
                bool leftChanged = previousMoveResizeGeom.left() != moveResizeGeom.left();
                bool rightChanged = previousMoveResizeGeom.right() != moveResizeGeom.right();
                bool topChanged = previousMoveResizeGeom.top() != moveResizeGeom.top();
                bool btmChanged = previousMoveResizeGeom.bottom() != moveResizeGeom.bottom();
                auto fixChangedState
                    = [titleFailed](bool& major, bool& counter, bool& ad1, bool& ad2) {
                          counter = false;
                          if (titleFailed)
                              major = false;
                          if (major)
                              ad1 = ad2 = false;
                      };
                fixChangedState(topChanged, btmChanged, leftChanged, rightChanged);

                if (topChanged)
                    moveResizeGeom.setTop(moveResizeGeom.y()
                                          + sign(previousMoveResizeGeom.y() - moveResizeGeom.y()));
                else if (leftChanged)
                    moveResizeGeom.setLeft(moveResizeGeom.x()
                                           + sign(previousMoveResizeGeom.x() - moveResizeGeom.x()));
                else if (btmChanged)
                    moveResizeGeom.setBottom(
                        moveResizeGeom.bottom()
                        + sign(previousMoveResizeGeom.bottom() - moveResizeGeom.bottom()));
                else if (rightChanged)
                    moveResizeGeom.setRight(
                        moveResizeGeom.right()
                        + sign(previousMoveResizeGeom.right() - moveResizeGeom.right()));
                else
                    break; // no position changed - that's certainly not good
                mov_res.geometry = moveResizeGeom;
            }
        }

        // Always obey size hints, even when in "unrestricted" mode
        auto size = adjusted_frame_size(win, mov_res.geometry.size(), sizeMode);

        // the new topleft and bottomright corners (after checking size constrains), if they'll be
        // needed

        topleft = QPoint(mov_res.geometry.right() - size.width() + 1,
                         mov_res.geometry.bottom() - size.height() + 1);
        bottomright = QPoint(mov_res.geometry.left() + size.width() - 1,
                             mov_res.geometry.top() + size.height() - 1);
        orig = mov_res.geometry;

        // if aspect ratios are specified, both dimensions may change.
        // Therefore grow to the right/bottom if needed.
        // TODO it should probably obey gravity rather than always using right/bottom ?
        if (sizeMode == size_mode::fixed_height) {
            orig.setRight(bottomright.x());
        } else if (sizeMode == size_mode::fixed_width) {
            orig.setBottom(bottomright.y());
        }

        calculateMoveResizeGeom();

        if (mov_res.geometry.size() != previousMoveResizeGeom.size()) {
            update = true;
        }
    } else if (is_move(win)) {
        Q_ASSERT(mode == position::center);
        if (!win->isMovable()) {
            // isMovableAcrossScreens() must have been true to get here
            // Special moving of maximized windows on Xinerama screens
            auto output = base::get_nearest_output(win->space.base.outputs, globalPos);
            if (win->control->fullscreen)
                mov_res.geometry = space_window_area(win->space, FullScreenArea, output, 0);
            else {
                auto moveResizeGeom = space_window_area(win->space, MaximizeArea, output, 0);
                auto adjSize = adjusted_frame_size(win, moveResizeGeom.size(), size_mode::max);
                if (adjSize != moveResizeGeom.size()) {
                    QRect r(moveResizeGeom);
                    moveResizeGeom.setSize(adjSize);
                    moveResizeGeom.moveCenter(r.center());
                }
                mov_res.geometry = moveResizeGeom;
            }
        } else {
            // first move, then snap, then check bounds
            auto moveResizeGeom = mov_res.geometry;
            moveResizeGeom.moveTopLeft(topleft);
            moveResizeGeom.moveTopLeft(adjust_window_position(
                win->space, *win, moveResizeGeom.topLeft(), mov_res.unrestricted));
            mov_res.geometry = moveResizeGeom;

            if (!mov_res.unrestricted) {
                // Strut areas
                auto const strut
                    = restricted_move_area(win->space, get_desktop(*win), strut_area::all);

                // On the screen
                QRegion availableArea(space_window_area(win->space, FullArea, nullptr, 0));

                // Strut areas
                availableArea -= strut;
                bool transposed = false;
                int requiredPixels;
                QRect bTitleRect = titleBarRect(transposed, requiredPixels);
                for (;;) {
                    auto moveResizeGeom = mov_res.geometry;
                    const QRect titleRect(bTitleRect.translated(moveResizeGeom.topLeft()));
                    int visiblePixels = 0;
                    for (auto const& rect : availableArea) {
                        const QRect r = rect & titleRect;
                        if ((transposed && r.width() == titleRect.width())
                            || // Only the full size regions...
                            (!transposed
                             && r.height() == titleRect.height())) // ...prevents long slim areas
                            visiblePixels += r.width() * r.height();
                    }
                    if (visiblePixels >= requiredPixels)
                        break; // We have reached a valid position

                    // (esp.) if there're more screens with different struts (panels) it the
                    // titlebar will be movable outside the movearea (covering one of the panels)
                    // until it crosses the panel "too much" (not enough visiblePixels) and then
                    // stucks because it's usually only pushed by 1px to either direction so we
                    // first check whether we intersect suc strut and move the window below it
                    // immediately (it's still possible to hit the visiblePixels >= titlebarArea
                    // break by moving the window slightly downwards, but it won't stuck) see bug
                    // #274466 and bug #301805 for why we can't just match the titlearea against the
                    // screen
                    if (win->space.base.outputs.size() > 1) {
                        // TODO: could be useful on partial screen struts (half-width panels etc.)
                        int newTitleTop = -1;
                        for (auto const& r : strut) {
                            if (r.top() == 0 && r.width() > r.height() && // "top panel"
                                r.intersects(moveResizeGeom) && moveResizeGeom.top() < r.bottom()) {
                                newTitleTop = r.bottom() + 1;
                                break;
                            }
                        }
                        if (newTitleTop > -1) {
                            moveResizeGeom.moveTop(
                                newTitleTop); // invalid position, possibly on screen change
                            mov_res.geometry = moveResizeGeom;
                            break;
                        }
                    }

                    int dx = sign(previousMoveResizeGeom.x() - moveResizeGeom.x()),
                        dy = sign(previousMoveResizeGeom.y() - moveResizeGeom.y());
                    if (visiblePixels
                        && dx) // means there's no full width cap -> favor horizontally
                        dy = 0;
                    else if (dy)
                        dx = 0;

                    // Move it back
                    moveResizeGeom.translate(dx, dy);
                    mov_res.geometry = moveResizeGeom;

                    if (moveResizeGeom == previousMoveResizeGeom) {
                        break; // Prevent lockup
                    }
                }
            }
        }
        if (mov_res.geometry.topLeft() != previousMoveResizeGeom.topLeft()) {
            update = true;
        }
    } else
        abort();

    if (!update) {
        return;
    }

    if (is_resize(win) && !win->control->have_resize_effect) {
        win->doResizeSync();
    } else {
        perform_move_resize(win);
    }

    if (is_move(win)) {
        win->space.edges->check(globalPos, QDateTime::fromMSecsSinceEpoch(xTime(), Qt::UTC));
    }
}

template<typename Win>
auto move_resize(Win* win, QPoint const& local, QPoint const& global)
{
    auto const old_geo = win->geo.frame;
    auto const restore_geo = win->geo.restore.max;

    // We call move_resize_impl once and afterwards check if quicktiling has been altered by it.
    move_resize_impl(win, local.x(), local.y(), global.x(), global.y());

    // TODO(romangg): The fullscreen check here looks out of place.
    if (win->control->fullscreen || !is_move(win)) {
        return;
    }

    if (win->control->quicktiling == quicktiles::none) {
        // Quicktiling not engaged at the moment. Just check the maximization zones for resizable
        // windows and return.
        if (win->isResizable()) {
            check_quicktile_maximization_zones(win, global.x(), global.y());
        }
        return;
    }

    if (old_geo == win->geo.update.frame) {
        // No update. Nothing more to do.
        // TODO(romangg): is this check really sensbile? Check against some other geometry instead?
        return;
    }

    geometry_updates_blocker blocker(win);

    // Reset previous quicktile mode and adapt geometry.
    set_quicktile_mode(win, quicktiles::none, false);
    auto const old_restore_geo = restore_geo.isValid() ? restore_geo : win->geo.frame;

    auto& mov_res = win->control->move_resize;

    auto x_offset = mov_res.offset.x() / double(old_geo.width()) * old_restore_geo.width();
    auto y_offset = mov_res.offset.y() / double(old_geo.height()) * old_restore_geo.height();

    mov_res.offset = QPoint(x_offset, y_offset);

    if (!win->control->rules.checkMaximize(maximize_mode::restore)) {
        mov_res.geometry = old_restore_geo;
    }

    // Now call again into the implementation to update the position.
    move_resize_impl(win, local.x(), local.y(), global.x(), global.y());
}

template<typename Win>
void update_move_resize(Win* win, QPointF const& currentGlobalCursor)
{
    move_resize(win, win->geo.pos(), currentGlobalCursor.toPoint());
}

template<typename Win>
void finish_move_resize(Win* win, bool cancel)
{
    geometry_updates_blocker blocker(win);

    auto& mov_res = win->control->move_resize;

    auto const wasResize = is_resize(win);
    mov_res.enabled = false;
    win->leaveMoveResize();

    if (cancel) {
        win->setFrameGeometry(mov_res.initial_geometry);
    } else {
        auto const& moveResizeGeom = mov_res.geometry;
        if (wasResize) {
            auto mode = win->geo.update.max_mode;
            if ((mode == maximize_mode::horizontal
                 && moveResizeGeom.width() != mov_res.initial_geometry.width())
                || (mode == maximize_mode::vertical
                    && moveResizeGeom.height() != mov_res.initial_geometry.height())) {
                mode = maximize_mode::restore;
            }
            win->update_maximized(mode);
        }
        win->setFrameGeometry(moveResizeGeom);
    }

    // Needs to be done because clientFinishUserMovedResized has not yet re-activated online
    // alignment.
    check_screen(*win);

    int output_index = win->topo.central_output
        ? base::get_output_index(win->space.base.outputs, *win->topo.central_output)
        : 0;
    if (output_index != mov_res.start_screen) {
        // Checks rule validity
        if (win->topo.central_output) {
            send_to_screen(win->space, win, *win->topo.central_output);
        }
        if (win->geo.update.max_mode != maximize_mode::restore) {
            check_workspace_position(win);
        }
    }

    if (win->control->electric_maximizing) {
        set_quicktile_mode(win, win->control->electric, false);
        set_electric_maximizing(win, false);
    }

    if (win->geo.update.max_mode == maximize_mode::restore
        && win->control->quicktiling == quicktiles::none && !win->geo.update.fullscreen) {
        win->geo.restore.max = QRect();
    }

    // FRAME    update();
    Q_EMIT win->qobject->clientFinishUserMovedResized();
}

template<typename Win>
void end_move_resize(Win* win)
{
    auto& mov_res = win->control->move_resize;

    mov_res.button_down = false;
    stop_delayed_move_resize(win);

    if (mov_res.enabled) {
        finish_move_resize(win, false);
        mov_res.contact = mouse_position(win);
    }

    update_cursor(win);
}

// TODO(romangg): We have 3 different functions to finish/end/leave a move-resize operation. There
//                should be only a single one!
template<typename Win>
void leave_move_resize(Win& win)
{
    set_move_resize_window(win.space, nullptr);
    win.control->move_resize.enabled = false;
    if (win.space.edges->desktop_switching.when_moving_client) {
        win.space.edges->reserveDesktopSwitching(false, Qt::Vertical | Qt::Horizontal);
    }
    if (win.control->electric_maximizing) {
        win.space.outline->hide();
        elevate(&win, false);
    }
}

template<typename Win>
void move(Win* win, QPoint const& point)
{
    assert(win->geo.update.pending == pending_geometry::none || win->geo.update.block);

    auto old_frame_geo = pending_frame_geometry(win);

    if (old_frame_geo.topLeft() == point) {
        return;
    }

    auto geo = old_frame_geo;
    geo.moveTopLeft(point);

    win->setFrameGeometry(geo);
}

template<typename Win>
void keep_in_area(Win* win, QRect area, bool partial)
{
    auto const frame_geo = pending_frame_geometry(win);
    auto pos = frame_geo.topLeft();
    auto size = frame_geo.size();

    if (partial) {
        // Increase the area so that can have only 100 pixels in the area.
        area.setLeft(std::min(pos.x() - size.width() + 100, area.left()));
        area.setTop(std::min(area.top() - size.height() + 100, area.top()));
        area.setRight(std::max(area.right() + size.width() - 100, area.right()));
        area.setBottom(std::max(area.bottom() + size.height() - 100, area.bottom()));
    } else if (area.width() < size.width() || area.height() < size.height()) {
        // Resize to fit into area.
        constrained_resize(
            win,
            QSize(std::min(area.width(), size.width()), std::min(area.height(), size.height())));

        pos = win->geo.update.frame.topLeft();
        size = win->geo.update.frame.size();
    }

    auto tx = pos.x();
    auto ty = pos.y();

    if (pos.x() + size.width() > area.right() && size.width() <= area.width()) {
        tx = area.right() - size.width() + 1;
    }
    if (pos.y() + size.height() > area.bottom() && size.height() <= area.height()) {
        ty = area.bottom() - size.height() + 1;
    }
    if (!area.contains(pos)) {
        if (tx < area.x()) {
            tx = area.x();
        }
        if (ty < area.y()) {
            ty = area.y();
        }
    }
    if (tx != pos.x() || ty != pos.y()) {
        move(win, QPoint(tx, ty));
    }
}

/**
 * Helper for workspace window packing. Checks for screen validity and updates in maximization case
 * as with normal moving.
 */
template<typename Win>
void pack_to(Win* win, int left, int top)
{
    // May cause leave event.
    win->space.focusMousePos = win->space.input->cursor->pos();

    auto const old_screen = win->topo.central_output;
    move(win, QPoint(left, top));
    assert(win->topo.central_output);

    if (win->topo.central_output != old_screen) {
        // Checks rule validity.
        send_to_screen(win->space, win, *win->topo.central_output);
        if (win->maximizeMode() != win::maximize_mode::restore) {
            check_workspace_position(win);
        }
    }
}

/**
 * When user presses on titlebar don't move immediately because it may just be a click.
 */
template<typename Win>
void start_delayed_move_resize(Win* win)
{
    auto& mov_res = win->control->move_resize;
    assert(!mov_res.delay_timer);

    mov_res.delay_timer = new QTimer(win->qobject.get());
    mov_res.delay_timer->setSingleShot(true);
    QObject::connect(mov_res.delay_timer, &QTimer::timeout, win->qobject.get(), [win]() {
        auto& mov_res = win->control->move_resize;
        assert(mov_res.button_down);
        if (!start_move_resize(win)) {
            mov_res.button_down = false;
        }
        update_cursor(win);
        stop_delayed_move_resize(win);
    });
    mov_res.delay_timer->start(QApplication::startDragTime());
}

template<typename Space, typename Win, typename Output>
void send_to_screen(Space const& space, Win* win, Output const& output)
{
    auto checked_output = win->control->rules.checkScreen(space.base, &output);

    if (win->control->active) {
        base::set_current_output(space.base, checked_output);

        // might impact the layer of a fullscreen window
        for (auto cc : space.windows) {
            if (cc->control && cc->control->fullscreen
                && cc->topo.central_output == checked_output) {
                update_layer(cc);
            }
        }
    }

    if (win->topo.central_output == checked_output) {
        // Don't use isOnScreen(), that's true even when only partially.
        return;
    }

    geometry_updates_blocker blocker(win);

    // operating on the maximized / quicktiled window would leave the old geom_restore behind,
    // so we clear the state first
    auto const old_restore_geo = win->geo.restore.max;
    auto const old_frame_geo = win->geo.update.frame;
    auto frame_geo = old_restore_geo.isValid() ? old_restore_geo : old_frame_geo;

    auto max_mode = win->geo.update.max_mode;
    auto qtMode = win->control->quicktiling;
    if (max_mode != maximize_mode::restore) {
        maximize(win, win::maximize_mode::restore);
    }

    if (qtMode != quicktiles::none) {
        set_quicktile_mode(win, quicktiles::none, true);
    }

    auto oldScreenArea = space_window_area(space, MaximizeArea, win);
    auto screenArea = space_window_area(space, MaximizeArea, checked_output, get_desktop(*win));

    // the window can have its center so that the position correction moves the new center onto
    // the old screen, what will tile it where it is. Ie. the screen is not changed
    // this happens esp. with electric border quicktiling
    if (qtMode != quicktiles::none) {
        keep_in_area(win, oldScreenArea, false);
    }

    // Move the window to have the same relative position to the center of the screen
    // (i.e. one near the middle of the right edge will also end up near the middle of the right
    // edge).
    auto center = frame_geo.center() - oldScreenArea.center();
    center.setX(center.x() * screenArea.width() / oldScreenArea.width());
    center.setY(center.y() * screenArea.height() / oldScreenArea.height());
    center += screenArea.center();
    frame_geo.moveCenter(center);

    win->setFrameGeometry(frame_geo);

    // If the window was inside the old screen area, explicitly make sure its inside also the new
    // screen area. Calling checkWorkspacePosition() should ensure that, but when moving to a small
    // screen the window could be big enough to overlap outside of the new screen area, making
    // struts from other screens come into effect, which could alter the resulting geometry.
    if (oldScreenArea.contains(old_frame_geo)) {
        keep_in_area(win, screenArea, false);
    }

    // The call to check_workspace_position(..) does change up the geometry-update again, making it
    // possibly the size of the whole screen. Therefore rememeber the current geometry for if
    // required setting later the restore geometry here.
    auto const restore_geo = win->geo.update.frame;

    check_workspace_position(win, old_frame_geo);

    // finally reset special states
    // NOTICE that MaximizeRestore/quicktiles::none checks are required.
    // eg. setting quicktiles::none would break maximization
    if (max_mode != maximize_mode::restore) {
        maximize(win, max_mode);
        win->geo.restore.max = restore_geo;
    }

    if (qtMode != quicktiles::none && qtMode != win->control->quicktiling) {
        set_quicktile_mode(win, qtMode, true);
        win->geo.restore.max = restore_geo;
    }

    auto children = restacked_by_space_stacking_order(&space, win->transient->children);
    for (auto const& child : children) {
        if (child->control) {
            send_to_screen(space, child, *checked_output);
        }
    }
}

}
