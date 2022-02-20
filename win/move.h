/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "deco.h"
#include "geo.h"
#include "net.h"
#include "space.h"
#include "types.h"

#include "base/output_helpers.h"
#include "base/platform.h"
#include "input/cursor.h"
#include "render/outline.h"
#include "screen_edges.h"

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

template<typename Win>
void update_cursor(Win* win)
{
    auto& mov_res = win->control->move_resize();
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
    Q_EMIT win->moveResizeCursorChanged(shape);
}

/**
 * Returns @c true if @p win is being interactively resized; otherwise @c false.
 */
template<typename Win>
bool is_resize(Win* win)
{
    auto const& mov_res = win->control->move_resize();
    return mov_res.enabled && mov_res.contact != position::center;
}

// This function checks if it actually makes sense to perform a restricted move/resize.
// If e.g. the titlebar is already outside of the workarea, there's no point in performing
// a restricted move resize, because then e.g. resize would also move the window (#74555).
template<typename Win>
void check_unrestricted_move_resize(Win* win)
{
    auto& mov_res = win->control->move_resize();
    if (mov_res.unrestricted) {
        return;
    }

    auto desktopArea = workspace()->clientArea(WorkArea, mov_res.geometry.center(), win->desktop());
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

inline void check_offscreen_position(QRect& frame_geo, const QRect& screenArea)
{
    if (frame_geo.left() > screenArea.right()) {
        frame_geo.moveLeft(screenArea.right() - screenArea.width() / 4);
    } else if (frame_geo.right() < screenArea.left()) {
        frame_geo.moveRight(screenArea.left() + screenArea.width() / 4);
    }
    if (frame_geo.top() > screenArea.bottom()) {
        frame_geo.moveTop(screenArea.bottom() - screenArea.height() / 4);
    } else if (frame_geo.bottom() < screenArea.top()) {
        frame_geo.moveBottom(screenArea.top() + screenArea.width() / 4);
    }
}

template<typename Win>
void check_workspace_position(Win* win,
                              QRect old_frame_geo = QRect(),
                              int oldDesktop = -2,
                              QRect old_client_geo = QRect())
{
    assert(win->control);

    if (is_desktop(win)) {
        return;
    }
    if (is_dock(win)) {
        return;
    }

    if (kwinApp()->get_base().get_outputs().empty()) {
        return;
    }

    if (win->geometry_update.fullscreen) {
        auto area = workspace()->clientArea(FullScreenArea, win);
        win->setFrameGeometry(area);
        return;
    }

    if (win->maximizeMode() != maximize_mode::restore) {
        geometry_updates_blocker block(win);

        win->update_maximized(win->geometry_update.max_mode);
        auto const screenArea = workspace()->clientArea(ScreenArea, win);

        auto geo = pending_frame_geometry(win);
        check_offscreen_position(geo, screenArea);
        win->setFrameGeometry(geo);

        return;
    }

    if (win->control->quicktiling() != quicktiles::none) {
        win->setFrameGeometry(electric_border_maximize_geometry(
            win, pending_frame_geometry(win).center(), win->desktop()));
        return;
    }

    enum { Left = 0, Top, Right, Bottom };
    int const border[4]
        = {left_border(win), top_border(win), right_border(win), bottom_border(win)};

    if (!old_frame_geo.isValid()) {
        old_frame_geo = pending_frame_geometry(win);
    }
    if (oldDesktop == -2) {
        oldDesktop = win->desktop();
    }
    if (!old_client_geo.isValid()) {
        old_client_geo
            = old_frame_geo.adjusted(border[Left], border[Top], -border[Right], -border[Bottom]);
    }

    // If the window was touching an edge before but not now move it so it is again.
    // Old and new maximums have different starting values so windows on the screen
    // edge will move when a new strut is placed on the edge.
    QRect old_screen_area;
    if (workspace()->inUpdateClientArea()) {
        // we need to find the screen area as it was before the change
        old_screen_area
            = QRect(0, 0, workspace()->oldDisplayWidth(), workspace()->oldDisplayHeight());
        int distance = INT_MAX;
        for (auto const& r : workspace()->previousScreenSizes()) {
            int d = r.contains(old_frame_geo.center())
                ? 0
                : (r.center() - old_frame_geo.center()).manhattanLength();
            if (d < distance) {
                distance = d;
                old_screen_area = r;
            }
        }
    } else {
        old_screen_area = workspace()->clientArea(ScreenArea, old_frame_geo.center(), oldDesktop);
    }

    // With full screen height.
    auto const old_tall_frame_geo = QRect(
        old_frame_geo.x(), old_screen_area.y(), old_frame_geo.width(), old_screen_area.height());

    // With full screen width.
    auto const old_wide_frame_geo = QRect(
        old_screen_area.x(), old_frame_geo.y(), old_screen_area.width(), old_frame_geo.height());

    auto old_top_max = old_screen_area.y();
    auto old_right_max = old_screen_area.x() + old_screen_area.width();
    auto old_bottom_max = old_screen_area.y() + old_screen_area.height();
    auto old_left_max = old_screen_area.x();

    auto const screenArea
        = workspace()->clientArea(ScreenArea, pending_frame_geometry(win).center(), win->desktop());

    auto top_max = screenArea.y();
    auto right_max = screenArea.x() + screenArea.width();
    auto bottom_max = screenArea.y() + screenArea.height();
    auto left_max = screenArea.x();

    auto frame_geo = pending_frame_geometry(win);
    auto client_geo
        = frame_geo.adjusted(border[Left], border[Top], -border[Right], -border[Bottom]);

    // Full screen height
    auto const tall_frame_geo
        = QRect(frame_geo.x(), screenArea.y(), frame_geo.width(), screenArea.height());

    // Full screen width
    auto const wide_frame_geo
        = QRect(screenArea.x(), frame_geo.y(), screenArea.width(), frame_geo.height());

    // Get the max strut point for each side where the window is (E.g. Highest point for
    // the bottom struts bounded by the window's left and right sides).

    // Default is to use restrictedMoveArea. That's on active desktop or screen change.
    auto moveAreaFunc = &space::restrictedMoveArea;
    if (workspace()->inUpdateClientArea()) {
        // On restriected area changes.
        // TODO(romangg): This check back on inUpdateClientArea and then setting here internally a
        //                different function is bad design. Replace with an argument or something.
        moveAreaFunc = &space::previousRestrictedMoveArea;
    }

    // These 4 compute old bounds.
    for (auto const& r : (workspace()->*moveAreaFunc)(oldDesktop, strut_area::top)) {
        auto rect = r & old_tall_frame_geo;
        if (!rect.isEmpty()) {
            old_top_max = std::max(old_top_max, rect.y() + rect.height());
        }
    }
    for (auto const& r : (workspace()->*moveAreaFunc)(oldDesktop, strut_area::right)) {
        auto rect = r & old_wide_frame_geo;
        if (!rect.isEmpty()) {
            old_right_max = std::min(old_right_max, rect.x());
        }
    }
    for (auto const& r : (workspace()->*moveAreaFunc)(oldDesktop, strut_area::bottom)) {
        auto rect = r & old_tall_frame_geo;
        if (!rect.isEmpty()) {
            old_bottom_max = std::min(old_bottom_max, rect.y());
        }
    }
    for (auto const& r : (workspace()->*moveAreaFunc)(oldDesktop, strut_area::left)) {
        auto rect = r & old_wide_frame_geo;
        if (!rect.isEmpty()) {
            old_left_max = std::max(old_left_max, rect.x() + rect.width());
        }
    }

    // These 4 compute new bounds.
    for (auto const& r : workspace()->restrictedMoveArea(win->desktop(), strut_area::top)) {
        auto rect = r & tall_frame_geo;
        if (!rect.isEmpty()) {
            top_max = std::max(top_max, rect.y() + rect.height());
        }
    }
    for (auto const& r : workspace()->restrictedMoveArea(win->desktop(), strut_area::right)) {
        auto rect = r & wide_frame_geo;
        if (!rect.isEmpty()) {
            right_max = std::min(right_max, rect.x());
        }
    }
    for (auto const& r : workspace()->restrictedMoveArea(win->desktop(), strut_area::bottom)) {
        auto rect = r & tall_frame_geo;
        if (!rect.isEmpty()) {
            bottom_max = std::min(bottom_max, rect.y());
        }
    }
    for (auto const& r : workspace()->restrictedMoveArea(win->desktop(), strut_area::left)) {
        auto rect = r & wide_frame_geo;
        if (!rect.isEmpty()) {
            left_max = std::max(left_max, rect.x() + rect.width());
        }
    }

    // Check if the sides were inside or touching but are no longer
    bool keep[4] = {false, false, false, false};
    bool save[4] = {false, false, false, false};
    int padding[4] = {0, 0, 0, 0};

    if (old_frame_geo.x() >= old_left_max) {
        save[Left] = frame_geo.x() < left_max;
    }

    if (old_frame_geo.x() == old_left_max) {
        keep[Left] = frame_geo.x() != left_max;
    } else if (old_client_geo.x() == old_left_max && client_geo.x() != left_max) {
        padding[0] = border[Left];
        keep[Left] = true;
    }

    if (old_frame_geo.y() >= old_top_max) {
        save[Top] = frame_geo.y() < top_max;
    }

    if (old_frame_geo.y() == old_top_max) {
        keep[Top] = frame_geo.y() != top_max;
    } else if (old_client_geo.y() == old_top_max && client_geo.y() != top_max) {
        padding[1] = border[Left];
        keep[Top] = true;
    }

    if (old_frame_geo.right() <= old_right_max - 1) {
        save[Right] = frame_geo.right() > right_max - 1;
    }

    if (old_frame_geo.right() == old_right_max - 1) {
        keep[Right] = frame_geo.right() != right_max - 1;
    } else if (old_client_geo.right() == old_right_max - 1 && client_geo.right() != right_max - 1) {
        padding[2] = border[Right];
        keep[Right] = true;
    }

    if (old_frame_geo.bottom() <= old_bottom_max - 1) {
        save[Bottom] = frame_geo.bottom() > bottom_max - 1;
    }

    if (old_frame_geo.bottom() == old_bottom_max - 1) {
        keep[Bottom] = frame_geo.bottom() != bottom_max - 1;
    } else if (old_client_geo.bottom() == old_bottom_max - 1
               && client_geo.bottom() != bottom_max - 1) {
        padding[3] = border[Bottom];
        keep[Bottom] = true;
    }

    // if randomly touches opposing edges, do not favor either
    if (keep[Left] && keep[Right]) {
        keep[Left] = keep[Right] = false;
        padding[0] = padding[2] = 0;
    }
    if (keep[Top] && keep[Bottom]) {
        keep[Top] = keep[Bottom] = false;
        padding[1] = padding[3] = 0;
    }

    auto const& outputs = kwinApp()->get_base().get_outputs();

    if (save[Left] || keep[Left]) {
        frame_geo.moveLeft(std::max(left_max, screenArea.x()) - padding[0]);
    }
    if (padding[0] && base::get_intersecting_outputs(outputs, frame_geo).size() > 1) {
        frame_geo.moveLeft(frame_geo.left() + padding[0]);
    }
    if (save[Top] || keep[Top]) {
        frame_geo.moveTop(std::max(top_max, screenArea.y()) - padding[1]);
    }
    if (padding[1] && base::get_intersecting_outputs(outputs, frame_geo).size() > 1) {
        frame_geo.moveTop(frame_geo.top() + padding[1]);
    }
    if (save[Right] || keep[Right]) {
        frame_geo.moveRight(std::min(right_max - 1, screenArea.right()) + padding[2]);
    }
    if (padding[2] && base::get_intersecting_outputs(outputs, frame_geo).size() > 1) {
        frame_geo.moveRight(frame_geo.right() - padding[2]);
    }
    if (old_frame_geo.x() >= old_left_max && frame_geo.x() < left_max) {
        frame_geo.setLeft(std::max(left_max, screenArea.x()));
    } else if (old_client_geo.x() >= old_left_max && frame_geo.x() + border[Left] < left_max) {
        frame_geo.setLeft(std::max(left_max, screenArea.x()) - border[Left]);
        if (base::get_intersecting_outputs(outputs, frame_geo).size() > 1) {
            frame_geo.setLeft(frame_geo.left() + border[Left]);
        }
    }
    if (save[Bottom] || keep[Bottom]) {
        frame_geo.moveBottom(std::min(bottom_max - 1, screenArea.bottom()) + padding[3]);
    }
    if (padding[3] && base::get_intersecting_outputs(outputs, frame_geo).size() > 1) {
        frame_geo.moveBottom(frame_geo.bottom() - padding[3]);
    }

    if (old_frame_geo.y() >= old_top_max && frame_geo.y() < top_max) {
        frame_geo.setTop(std::max(top_max, screenArea.y()));
    } else if (old_client_geo.y() >= old_top_max && frame_geo.y() + border[Top] < top_max) {
        frame_geo.setTop(std::max(top_max, screenArea.y()) - border[Top]);
        if (base::get_intersecting_outputs(outputs, frame_geo).size() > 1) {
            frame_geo.setTop(frame_geo.top() + border[Top]);
        }
    }

    check_offscreen_position(frame_geo, screenArea);

    // Obey size hints. TODO: We really should make sure it stays in the right place
    frame_geo.setSize(adjusted_frame_size(win, frame_geo.size(), size_mode::any));

    win->setFrameGeometry(frame_geo);
}

template<typename Win>
void set_maximize(Win* win, bool vertically, bool horizontally)
{
    auto mode = maximize_mode::restore;
    if (vertically) {
        mode |= maximize_mode::vertical;
    }
    if (horizontally) {
        mode |= maximize_mode::horizontal;
    }
    win->update_maximized(mode);
}

template<typename Win>
void maximize(Win* win, maximize_mode mode)
{
    win->update_maximized(mode);
}

/**
 * Checks if the mouse cursor is near the edge of the screen and if so
 * activates quick tiling or maximization.
 */
template<typename Win>
void check_quicktile_maximization_zones(Win* win, int xroot, int yroot)
{
    auto mode = quicktiles::none;
    bool inner_border = false;
    auto const& outputs = kwinApp()->get_base().get_outputs();

    for (size_t i = 0; i < outputs.size(); ++i) {
        if (!outputs.at(i)->geometry().contains(QPoint(xroot, yroot))) {
            continue;
        }

        auto in_screen = [i, &outputs](const QPoint& pt) {
            for (size_t j = 0; j < outputs.size(); ++j) {
                if (j != i && outputs.at(j)->geometry().contains(pt)) {
                    return true;
                }
            }
            return false;
        };

        auto area = workspace()->clientArea(MaximizeArea, QPoint(xroot, yroot), win->desktop());
        if (kwinApp()->options->electricBorderTiling()) {
            if (xroot <= area.x() + 20) {
                mode |= quicktiles::left;
                inner_border = in_screen(QPoint(area.x() - 1, yroot));
            } else if (xroot >= area.x() + area.width() - 20) {
                mode |= quicktiles::right;
                inner_border = in_screen(QPoint(area.right() + 1, yroot));
            }
        }

        if (mode != quicktiles::none) {
            if (yroot <= area.y() + area.height() * kwinApp()->options->electricBorderCornerRatio())
                mode |= quicktiles::top;
            else if (yroot >= area.y() + area.height()
                         - area.height() * kwinApp()->options->electricBorderCornerRatio())
                mode |= quicktiles::bottom;
        } else if (kwinApp()->options->electricBorderMaximize() && yroot <= area.y() + 5
                   && win->isMaximizable()) {
            mode = quicktiles::maximize;
            inner_border = in_screen(QPoint(xroot, area.y() - 1));
        }
        break;
    }
    if (mode != win->control->electric()) {
        set_electric(win, mode);
        if (inner_border) {
            delayed_electric_maximize(win);
        } else {
            set_electric_maximizing(win, mode != quicktiles::none);
        }
    }
}

/**
 * Sets the quick tile mode ("snap") of this window.
 * This will also handle preserving and restoring of window geometry as necessary.
 * @param mode The tile mode (left/right) to give this window.
 * @param keyboard Defines whether to take keyboard cursor into account.
 */
template<typename Win>
void set_quicktile_mode(Win* win, quicktiles mode, bool keyboard)
{
    // Only allow quick tile on a regular window.
    if (!win->isResizable()) {
        return;
    }

    // May cause leave event
    workspace()->updateFocusMousePosition(input::get_cursor()->pos());

    geometry_updates_blocker blocker(win);

    // Store current geometry if not already defined.
    if (!win->restore_geometries.maximize.isValid()) {
        win->restore_geometries.maximize = win->frameGeometry();
    }

    // Later calls to set_maximize(..) would reset the restore geometry.
    auto const old_restore_geo = win->restore_geometries.maximize;

    if (mode == quicktiles::maximize) {
        // Special case where we just maximize and return early.

        auto const old_quicktiling = win->control->quicktiling();
        win->control->set_quicktiling(quicktiles::none);

        if (win->maximizeMode() == maximize_mode::full) {
            // TODO(romangg): When window was already maximized we now "unmaximize" it. Why?
            set_maximize(win, false, false);
        } else {
            win->control->set_quicktiling(quicktiles::maximize);
            set_maximize(win, true, true);
            auto clientArea = workspace()->clientArea(MaximizeArea, win);

            if (auto frame_geo = pending_frame_geometry(win); frame_geo.top() != clientArea.top()) {
                frame_geo.moveTop(clientArea.top());
                win->setFrameGeometry(frame_geo);
            }
            win->restore_geometries.maximize = old_restore_geo;
        }

        if (old_quicktiling != win->control->quicktiling()) {
            Q_EMIT win->quicktiling_changed();
        }
        return;
    }

    // Sanitize the mode, ie. simplify "invalid" combinations.
    if ((mode & quicktiles::horizontal) == quicktiles::horizontal) {
        mode &= ~quicktiles::horizontal;
    }
    if ((mode & quicktiles::vertical) == quicktiles::vertical) {
        mode &= ~quicktiles::vertical;
    }

    // Used by electric_border_maximize_geometry(..).
    win->control->set_electric(mode);

    if (win->geometry_update.max_mode != maximize_mode::restore) {
        // Restore from maximized so that it is possible to tile maximized windows with one hit or
        // by dragging.
        if (mode != quicktiles::none) {
            // Temporary, so the maximize code doesn't get all confused
            win->control->set_quicktiling(quicktiles::none);

            set_maximize(win, false, false);

            auto ref_pos
                = keyboard ? pending_frame_geometry(win).center() : input::get_cursor()->pos();

            win->setFrameGeometry(electric_border_maximize_geometry(win, ref_pos, win->desktop()));
            // Store the mode change
            win->control->set_quicktiling(mode);
            win->restore_geometries.maximize = old_restore_geo;
        } else {
            win->control->set_quicktiling(mode);
            set_maximize(win, false, false);
        }

        Q_EMIT win->quicktiling_changed();
        return;
    }

    if (mode != quicktiles::none) {
        auto target_screen
            = keyboard ? pending_frame_geometry(win).center() : input::get_cursor()->pos();

        if (win->control->quicktiling() == mode) {
            // If trying to tile to the side that the window is already tiled to move the window to
            // the next screen if it exists, otherwise toggle the mode (set quicktiles::none)

            // TODO(romangg): Once we use size_t consistently for screens identification replace
            // these (currentyl implicit casted) types with auto.
            auto const& outputs = kwinApp()->get_base().get_outputs();
            auto const old_screen = win->central_output
                ? base::get_output_index(kwinApp()->get_base().get_outputs(), *win->central_output)
                : 0;
            auto screen = old_screen;

            std::vector<QRect> screens_geos(outputs.size());
            screens_geos.resize(outputs.size());

            for (size_t i = 0; i < outputs.size(); ++i) {
                // Geometry cache.
                screens_geos[i] = outputs.at(i)->geometry();
            }

            for (size_t i = 0; i < outputs.size(); ++i) {
                if (i == old_screen) {
                    continue;
                }

                if (screens_geos[i].bottom() <= screens_geos[old_screen].top()
                    || screens_geos[i].top() >= screens_geos[old_screen].bottom()) {
                    // Not in horizontal line
                    continue;
                }

                auto const x = screens_geos[i].center().x();
                if ((mode & quicktiles::horizontal) == quicktiles::left) {
                    if (x >= screens_geos[old_screen].center().x()
                        || (old_screen != screen && x <= screens_geos[screen].center().x())) {
                        // Not left of current or more left then found next
                        continue;
                    }
                } else if ((mode & quicktiles::horizontal) == quicktiles::right) {
                    if (x <= screens_geos[old_screen].center().x()
                        || (old_screen != screen && x >= screens_geos[screen].center().x())) {
                        // Not right of current or more right then found next.
                        continue;
                    }
                }

                screen = i;
            }

            if (screen == old_screen) {
                // No other screens, toggle tiling.
                mode = quicktiles::none;
            } else {
                // Move to other screen
                win->setFrameGeometry(win->restore_geometries.maximize.translated(
                    screens_geos[screen].topLeft() - screens_geos[old_screen].topLeft()));
                target_screen = screens_geos[screen].center();

                // Swap sides
                if (flags(mode & quicktiles::horizontal)) {
                    mode = (~mode & quicktiles::horizontal) | (mode & quicktiles::vertical);
                }
            }

            // used by electric_border_maximize_geometry(.)
            set_electric(win, mode);

        } else if (win->control->quicktiling() == quicktiles::none) {
            // Not coming out of an existing tile, not shifting monitors, we're setting a brand new
            // tile. Store geometry first, so we can go out of this tile later.
            if (!win->restore_geometries.maximize.isValid()) {
                win->restore_geometries.maximize = win->frameGeometry();
            }
        }

        if (mode != quicktiles::none) {
            win->control->set_quicktiling(mode);
            // Temporary, so the maximize code doesn't get all confused
            win->control->set_quicktiling(quicktiles::none);

            // TODO(romangg): With decorations this was previously forced in order to handle borders
            //                being changed. Is it safe to do this now without that?
            win->setFrameGeometry(
                electric_border_maximize_geometry(win, target_screen, win->desktop()));
        }

        // Store the mode change
        win->control->set_quicktiling(mode);
    }

    if (mode == quicktiles::none) {
        win->control->set_quicktiling(quicktiles::none);
        win->setFrameGeometry(win->restore_geometries.maximize);

        // Just in case it's a different screen
        check_workspace_position(win);

        // If we're here we can unconditionally reset the restore geometry since we earlier excluded
        // the case of the window being maximized.
        win->restore_geometries.maximize = QRect();
    }

    Q_EMIT win->quicktiling_changed();
}

template<typename Win>
void stop_delayed_move_resize(Win* win)
{
    auto& mov_res = win->control->move_resize();
    delete mov_res.delay_timer;
    mov_res.delay_timer = nullptr;
}

template<typename Win>
bool start_move_resize(Win* win)
{
    auto& mov_res = win->control->move_resize();

    assert(!mov_res.enabled);
    assert(QWidget::keyboardGrabber() == nullptr);
    assert(QWidget::mouseGrabber() == nullptr);

    stop_delayed_move_resize(win);

    if (QApplication::activePopupWidget()) {
        // Popups have grab.
        return false;
    }
    if (win->control->fullscreen()
        && (kwinApp()->get_base().get_outputs().size() < 2 || !win->isMovableAcrossScreens())) {
        return false;
    }
    if (!win->doStartMoveResize()) {
        return false;
    }

    win->control->deco().double_click.stop();

    auto const mode = mov_res.contact;
    auto const was_maxed_full = win->maximizeMode() == maximize_mode::full;
    auto const was_tiled = win->control->quicktiling() != quicktiles::none;
    auto const was_fullscreen = win->geometry_update.fullscreen;

    if (mode == position::center) {
        // That's a move.
        // TODO(romangg): Shorten the following condition to just restore geometry being invalid?
        if (!was_maxed_full && !was_tiled && !was_fullscreen) {
            // Remember current geometry in case the window is later moved to an edge for tiling.
            win->restore_geometries.maximize = win->frameGeometry();
        }
    } else {
        // That's a resize.
        win->restore_geometries.maximize = win->frameGeometry();
        if (was_maxed_full) {
            set_maximize(win, false, false);
        }
        if (win->control->quicktiling() != quicktiles::none) {
            // Exit quick tile mode when the user attempts to resize a tiled window.
            set_quicktile_mode(win, quicktiles::none, false);
        }
        win->restore_geometries.maximize = QRect();
    }

    mov_res.enabled = true;
    workspace()->setMoveResizeClient(win);

    win->control->update_have_resize_effect();

    mov_res.initial_geometry = pending_frame_geometry(win);
    mov_res.geometry = mov_res.initial_geometry;
    mov_res.start_screen = win->central_output
        ? base::get_output_index(kwinApp()->get_base().get_outputs(), *win->central_output)
        : 0;

    check_unrestricted_move_resize(win);

    Q_EMIT win->clientStartUserMovedResized(win);

    if (workspace()->edges->desktop_switching.when_moving_client) {
        workspace()->edges->reserveDesktopSwitching(true, Qt::Vertical | Qt::Horizontal);
    }

    return true;
}

template<typename Win>
void perform_move_resize(Win* win)
{
    auto const& geom = win->control->move_resize().geometry;

    if (is_move(win)) {
        win->setFrameGeometry(geom);
    }

    win->doPerformMoveResize();
    Q_EMIT win->clientStepUserMovedResized(win, geom);
}

template<typename Win>
auto move_resize_impl(Win* win, int x, int y, int x_root, int y_root)
{
    if (win->isWaitingForMoveResizeSync()) {
        return;
    }

    auto& mov_res = win->control->move_resize();

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
        auto const& moveResizeGeom = win->control->move_resize().geometry;
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
            auto& mov_res = win->control->move_resize();
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
        mov_res.geometry = workspace()->adjustClientSize(win, mov_res.geometry, mode);

        if (!mov_res.unrestricted) {
            // Make sure the titlebar isn't behind a restricted area. We don't need to restrict
            // the other directions. If not visible enough, move the window to the closest valid
            // point. We bruteforce this by slowly moving the window back to its previous position
            QRegion availableArea(workspace()->clientArea(FullArea, nullptr, 0)); // On the screen
            availableArea -= workspace()->restrictedMoveArea(win->desktop());     // Strut areas
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
            auto output = base::get_nearest_output(kwinApp()->get_base().get_outputs(), globalPos);
            if (win->control->fullscreen())
                mov_res.geometry = workspace()->clientArea(FullScreenArea, output, 0);
            else {
                auto moveResizeGeom = workspace()->clientArea(MaximizeArea, output, 0);
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
            moveResizeGeom.moveTopLeft(workspace()->adjustClientPosition(
                win, moveResizeGeom.topLeft(), mov_res.unrestricted));
            mov_res.geometry = moveResizeGeom;

            if (!mov_res.unrestricted) {
                // Strut areas
                auto const strut = workspace()->restrictedMoveArea(win->desktop());

                // On the screen
                QRegion availableArea(workspace()->clientArea(FullArea, nullptr, 0));

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
                    if (kwinApp()->get_base().get_outputs().size() > 1) {
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

    if (is_resize(win) && !win->control->have_resize_effect()) {
        win->doResizeSync();
    } else {
        perform_move_resize(win);
    }

    if (is_move(win)) {
        workspace()->edges->check(globalPos, QDateTime::fromMSecsSinceEpoch(xTime(), Qt::UTC));
    }
}

template<typename Win>
auto move_resize(Win* win, QPoint const& local, QPoint const& global)
{
    auto const old_geo = win->frameGeometry();
    auto const restore_geo = win->restore_geometries.maximize;

    // We call move_resize_impl once and afterwards check if quicktiling has been altered by it.
    move_resize_impl(win, local.x(), local.y(), global.x(), global.y());

    // TODO(romangg): The fullscreen check here looks out of place.
    if (win->control->fullscreen() || !is_move(win)) {
        return;
    }

    if (win->control->quicktiling() == quicktiles::none) {
        // Quicktiling not engaged at the moment. Just check the maximization zones for resizable
        // windows and return.
        if (win->isResizable()) {
            check_quicktile_maximization_zones(win, global.x(), global.y());
        }
        return;
    }

    if (old_geo == win->geometry_update.frame) {
        // No update. Nothing more to do.
        // TODO(romangg): is this check really sensbile? Check against some other geometry instead?
        return;
    }

    geometry_updates_blocker blocker(win);

    // Reset previous quicktile mode and adapt geometry.
    set_quicktile_mode(win, quicktiles::none, false);
    auto const old_restore_geo = restore_geo.isValid() ? restore_geo : win->frameGeometry();

    auto& mov_res = win->control->move_resize();

    auto x_offset = mov_res.offset.x() / double(old_geo.width()) * old_restore_geo.width();
    auto y_offset = mov_res.offset.y() / double(old_geo.height()) * old_restore_geo.height();

    mov_res.offset = QPoint(x_offset, y_offset);

    if (!win->control->rules().checkMaximize(maximize_mode::restore)) {
        mov_res.geometry = old_restore_geo;
    }

    // Now call again into the implementation to update the position.
    move_resize_impl(win, local.x(), local.y(), global.x(), global.y());
}

template<typename Win>
void update_move_resize(Win* win, QPointF const& currentGlobalCursor)
{
    move_resize(win, win->pos(), currentGlobalCursor.toPoint());
}

template<typename Win>
void finish_move_resize(Win* win, bool cancel)
{
    geometry_updates_blocker blocker(win);

    auto& mov_res = win->control->move_resize();

    auto const wasResize = is_resize(win);
    mov_res.enabled = false;
    win->leaveMoveResize();

    if (cancel) {
        win->setFrameGeometry(mov_res.initial_geometry);
    } else {
        auto const& moveResizeGeom = mov_res.geometry;
        if (wasResize) {
            auto mode = win->geometry_update.max_mode;
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
    win->checkScreen();

    int output_index = win->central_output
        ? base::get_output_index(kwinApp()->get_base().get_outputs(), *win->central_output)
        : 0;
    if (output_index != mov_res.start_screen) {
        // Checks rule validity
        if (win->central_output) {
            workspace()->sendClientToScreen(win, *win->central_output);
        }
        if (win->geometry_update.max_mode != maximize_mode::restore) {
            check_workspace_position(win);
        }
    }

    if (win->control->electric_maximizing()) {
        set_quicktile_mode(win, win->control->electric(), false);
        set_electric_maximizing(win, false);
    }

    if (win->geometry_update.max_mode == maximize_mode::restore
        && win->control->quicktiling() == quicktiles::none && !win->geometry_update.fullscreen) {
        win->restore_geometries.maximize = QRect();
    }

    // FRAME    update();
    Q_EMIT win->clientFinishUserMovedResized(win);
}

template<typename Win>
void end_move_resize(Win* win)
{
    auto& mov_res = win->control->move_resize();

    mov_res.button_down = false;
    stop_delayed_move_resize(win);

    if (mov_res.enabled) {
        finish_move_resize(win, false);
        mov_res.contact = mouse_position(win);
    }

    update_cursor(win);
}

template<typename Win>
void move(Win* win, QPoint const& point)
{
    assert(win->geometry_update.pending == pending_geometry::none || win->geometry_update.block);

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

        pos = win->geometry_update.frame.topLeft();
        size = win->geometry_update.frame.size();
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
    workspace()->updateFocusMousePosition(input::get_cursor()->pos());

    auto const old_screen = win->central_output;
    move(win, QPoint(left, top));
    assert(win->central_output);

    if (win->central_output != old_screen) {
        // Checks rule validity.
        workspace()->sendClientToScreen(win, *win->central_output);
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
    auto& mov_res = win->control->move_resize();
    assert(!mov_res.delay_timer);

    mov_res.delay_timer = new QTimer(win);
    mov_res.delay_timer->setSingleShot(true);
    QObject::connect(mov_res.delay_timer, &QTimer::timeout, win, [win]() {
        auto& mov_res = win->control->move_resize();
        assert(mov_res.button_down);
        if (!start_move_resize(win)) {
            mov_res.button_down = false;
        }
        update_cursor(win);
        stop_delayed_move_resize(win);
    });
    mov_res.delay_timer->start(QApplication::startDragTime());
}

}
