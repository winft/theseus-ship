/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_WIN_MOVE_H
#define KWIN_WIN_MOVE_H

#include "cursor.h"
#include "deco.h"
#include "geo.h"
#include "net.h"
#include "outline.h"
#include "screenedge.h"
#include "screens.h"
#include "types.h"
#include "workspace.h"

#include <QWidget>

namespace KWin::win
{

template<typename Win>
class geometry_updates_blocker
{
public:
    explicit geometry_updates_blocker(Win* c)
        : cl(c)
    {
        block_geometry_updates(cl, true);
    }
    ~geometry_updates_blocker()
    {
        block_geometry_updates(cl, false);
    }

private:
    Win* cl;
};

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

    if (!win->isResizable() || win::shaded(win)) {
        contact = win::position::center;
    }
    CursorShape shape = Qt::ArrowCursor;
    switch (contact) {
    case win::position::top_left:
        shape = KWin::ExtendedCursor::SizeNorthWest;
        break;
    case win::position::bottom_right:
        shape = KWin::ExtendedCursor::SizeSouthEast;
        break;
    case win::position::bottom_left:
        shape = KWin::ExtendedCursor::SizeSouthWest;
        break;
    case win::position::top_right:
        shape = KWin::ExtendedCursor::SizeNorthEast;
        break;
    case win::position::top:
        shape = KWin::ExtendedCursor::SizeNorth;
        break;
    case win::position::bottom:
        shape = KWin::ExtendedCursor::SizeSouth;
        break;
    case win::position::left:
        shape = KWin::ExtendedCursor::SizeWest;
        break;
    case win::position::right:
        shape = KWin::ExtendedCursor::SizeEast;
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
// NOTE: Most of it is duplicated from move_resize().
template<typename Win>
void check_unrestricted_move_resize(Win* win)
{
    auto& mov_res = win->control->move_resize();
    if (mov_res.unrestricted) {
        return;
    }

    auto const& moveResizeGeom = mov_res.geometry;
    auto desktopArea = workspace()->clientArea(WorkArea, moveResizeGeom.center(), win->desktop());
    int left_marge, right_marge, top_marge, bottom_marge, titlebar_marge;

    // restricted move/resize - keep at least part of the titlebar always visible
    // how much must remain visible when moved away in that direction
    left_marge = std::min(100 + right_border(win), moveResizeGeom.width());
    right_marge = std::min(100 + left_border(win), moveResizeGeom.width());

    // width/height change with opaque resizing, use the initial ones
    titlebar_marge = mov_res.initial_geometry.height();
    top_marge = bottom_border(win);
    bottom_marge = top_border(win);

    auto has_unrestricted_resize = [&] {
        if (!is_resize(win)) {
            return false;
        }
        if (moveResizeGeom.bottom() < desktopArea.top() + top_marge) {
            return true;
        }
        if (moveResizeGeom.top() > desktopArea.bottom() - bottom_marge) {
            return true;
        }
        if (moveResizeGeom.right() < desktopArea.left() + left_marge) {
            return true;
        }
        if (moveResizeGeom.left() > desktopArea.right() - right_marge) {
            return true;
        }
        if (!mov_res.unrestricted && moveResizeGeom.top() < desktopArea.top()) {
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
        if (moveResizeGeom.bottom() < desktopArea.top() + titlebar_marge - 1) {
            return true;
        }

        // No need to check top_marge, titlebar_marge already handles it
        if (moveResizeGeom.top() > desktopArea.bottom() - bottom_marge + 1) {
            return true;
        }
        if (moveResizeGeom.right() < desktopArea.left() + left_marge) {
            return true;
        }
        if (moveResizeGeom.left() > desktopArea.right() - right_marge) {
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
    enum { Left = 0, Top, Right, Bottom };
    int const border[4]
        = {left_border(win), top_border(win), right_border(win), bottom_border(win)};

    if (!old_frame_geo.isValid()) {
        old_frame_geo = win->frameGeometry();
    }
    if (oldDesktop == -2) {
        oldDesktop = win->desktop();
    }
    if (!old_client_geo.isValid()) {
        old_client_geo
            = old_frame_geo.adjusted(border[Left], border[Top], -border[Right], -border[Bottom]);
    }

    if (is_desktop(win)) {
        return;
    }

    if (win->control->fullscreen()) {
        auto area = workspace()->clientArea(FullScreenArea, win);
        if (win->frameGeometry() != area) {
            win->setFrameGeometry(area);
        }
        return;
    }
    if (is_dock(win)) {
        return;
    }

    if (win->maximizeMode() != maximize_mode::restore) {
        geometry_updates_blocker block(win);
        // Adjust size
        win->changeMaximize(false, false, true);
        const QRect screenArea = workspace()->clientArea(ScreenArea, win);
        auto geom = win->frameGeometry();
        check_offscreen_position(geom, screenArea);
        win->setFrameGeometry(geom);
        return;
    }

    if (win->control->quicktiling() != quicktiles::none) {
        win->setFrameGeometry(
            electric_border_maximize_geometry(win, win->frameGeometry().center(), win->desktop()));
        return;
    }

    // this can be true only if this window was mapped before KWin
    // was started - in such case, don't adjust position to workarea,
    // because the window already had its position, and if a window
    // with a strut altering the workarea would be managed in initialization
    // after this one, this window would be moved
    if (!workspace() || workspace()->initializing()) {
        return;
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

    auto const screenArea = workspace()->clientArea(
        ScreenArea, win->restore_geometries.maximize.center(), win->desktop());

    auto top_max = screenArea.y();
    auto right_max = screenArea.x() + screenArea.width();
    auto bottom_max = screenArea.y() + screenArea.height();
    auto left_max = screenArea.x();
    auto frame_geo = win->restore_geometries.maximize;
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

    // These 4 compute old bounds ...
    auto moveAreaFunc = workspace()->inUpdateClientArea()
        ?
        //... the restricted areas changed
        &Workspace::previousRestrictedMoveArea
        :
        //... when e.g. active desktop or screen changes
        &Workspace::restrictedMoveArea;

    for (auto const& r : (workspace()->*moveAreaFunc)(oldDesktop, StrutAreaTop)) {
        auto rect = r & old_tall_frame_geo;
        if (!rect.isEmpty()) {
            old_top_max = std::max(old_top_max, rect.y() + rect.height());
        }
    }
    for (auto const& r : (workspace()->*moveAreaFunc)(oldDesktop, StrutAreaRight)) {
        auto rect = r & old_wide_frame_geo;
        if (!rect.isEmpty()) {
            old_right_max = std::min(old_right_max, rect.x());
        }
    }
    for (auto const& r : (workspace()->*moveAreaFunc)(oldDesktop, StrutAreaBottom)) {
        auto rect = r & old_tall_frame_geo;
        if (!rect.isEmpty()) {
            old_bottom_max = std::min(old_bottom_max, rect.y());
        }
    }
    for (auto const& r : (workspace()->*moveAreaFunc)(oldDesktop, StrutAreaLeft)) {
        auto rect = r & old_wide_frame_geo;
        if (!rect.isEmpty()) {
            old_left_max = std::max(old_left_max, rect.x() + rect.width());
        }
    }

    // These 4 compute new bounds
    for (auto const& r : workspace()->restrictedMoveArea(win->desktop(), StrutAreaTop)) {
        auto rect = r & tall_frame_geo;
        if (!rect.isEmpty()) {
            top_max = std::max(top_max, rect.y() + rect.height());
        }
    }
    for (auto const& r : workspace()->restrictedMoveArea(win->desktop(), StrutAreaRight)) {
        auto rect = r & wide_frame_geo;
        if (!rect.isEmpty()) {
            right_max = std::min(right_max, rect.x());
        }
    }
    for (auto const& r : workspace()->restrictedMoveArea(win->desktop(), StrutAreaBottom)) {
        auto rect = r & tall_frame_geo;
        if (!rect.isEmpty()) {
            bottom_max = std::min(bottom_max, rect.y());
        }
    }
    for (auto const& r : workspace()->restrictedMoveArea(win->desktop(), StrutAreaLeft)) {
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

    if (save[Left] || keep[Left]) {
        frame_geo.moveLeft(std::max(left_max, screenArea.x()) - padding[0]);
    }
    if (padding[0] && screens()->intersecting(frame_geo) > 1) {
        frame_geo.moveLeft(frame_geo.left() + padding[0]);
    }
    if (save[Top] || keep[Top]) {
        frame_geo.moveTop(std::max(top_max, screenArea.y()) - padding[1]);
    }
    if (padding[1] && screens()->intersecting(frame_geo) > 1) {
        frame_geo.moveTop(frame_geo.top() + padding[1]);
    }
    if (save[Right] || keep[Right]) {
        frame_geo.moveRight(std::min(right_max - 1, screenArea.right()) + padding[2]);
    }
    if (padding[2] && screens()->intersecting(frame_geo) > 1) {
        frame_geo.moveRight(frame_geo.right() - padding[2]);
    }
    if (old_frame_geo.x() >= old_left_max && frame_geo.x() < left_max) {
        frame_geo.setLeft(std::max(left_max, screenArea.x()));
    } else if (old_client_geo.x() >= old_left_max && frame_geo.x() + border[Left] < left_max) {
        frame_geo.setLeft(std::max(left_max, screenArea.x()) - border[Left]);
        if (screens()->intersecting(frame_geo) > 1) {
            frame_geo.setLeft(frame_geo.left() + border[Left]);
        }
    }
    if (save[Bottom] || keep[Bottom]) {
        frame_geo.moveBottom(std::min(bottom_max - 1, screenArea.bottom()) + padding[3]);
    }
    if (padding[3] && screens()->intersecting(frame_geo) > 1) {
        frame_geo.moveBottom(frame_geo.bottom() - padding[3]);
    }

    if (old_frame_geo.y() >= old_top_max && frame_geo.y() < top_max) {
        frame_geo.setTop(std::max(top_max, screenArea.y()));
    } else if (old_client_geo.y() >= old_top_max && frame_geo.y() + border[Top] < top_max) {
        frame_geo.setTop(std::max(top_max, screenArea.y()) - border[Top]);
        if (screens()->intersecting(frame_geo) > 1) {
            frame_geo.setTop(frame_geo.top() + border[Top]);
        }
    }

    check_offscreen_position(frame_geo, screenArea);

    // Obey size hints. TODO: We really should make sure it stays in the right place
    if (!shaded(win)) {
        frame_geo.setSize(adjusted_size(win, frame_geo.size(), size_mode::any));
    }
    if (frame_geo != win->frameGeometry()) {
        win->setFrameGeometry(frame_geo);
    }
}

template<typename Win>
void set_maximize(Win* win, bool vertically, bool horizontally)
{
    // set_maximize() flips the state, so change from set->flip
    auto const oldMode = win->maximizeMode();
    win->changeMaximize(flags(oldMode & maximize_mode::horizontal) ? !horizontally : horizontally,
                        flags(oldMode & maximize_mode::vertical) ? !vertically : vertically,
                        false);
    auto const newMode = win->maximizeMode();
    if (oldMode != newMode) {
        Q_EMIT win->clientMaximizedStateChanged(win, newMode);
        Q_EMIT win->clientMaximizedStateChanged(win, vertically, horizontally);
    }
}

template<typename Win>
void maximize(Win* win, maximize_mode mode)
{
    set_maximize(
        win, flags(mode & maximize_mode::vertical), flags(mode & maximize_mode::horizontal));
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

    for (int i = 0; i < screens()->count(); ++i) {
        if (!screens()->geometry(i).contains(QPoint(xroot, yroot))) {
            continue;
        }

        auto in_screen = [i](const QPoint& pt) {
            for (int j = 0; j < screens()->count(); ++j) {
                if (j != i && screens()->geometry(j).contains(pt)) {
                    return true;
                }
            }
            return false;
        };

        auto area = workspace()->clientArea(MaximizeArea, QPoint(xroot, yroot), win->desktop());
        if (options->electricBorderTiling()) {
            if (xroot <= area.x() + 20) {
                mode |= quicktiles::left;
                inner_border = in_screen(QPoint(area.x() - 1, yroot));
            } else if (xroot >= area.x() + area.width() - 20) {
                mode |= quicktiles::right;
                inner_border = in_screen(QPoint(area.right() + 1, yroot));
            }
        }

        if (mode != quicktiles::none) {
            if (yroot <= area.y() + area.height() * options->electricBorderCornerRatio())
                mode |= quicktiles::top;
            else if (yroot >= area.y() + area.height()
                         - area.height() * options->electricBorderCornerRatio())
                mode |= quicktiles::bottom;
        } else if (options->electricBorderMaximize() && yroot <= area.y() + 5
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
    workspace()->updateFocusMousePosition(Cursor::pos());

    geometry_updates_blocker blocker(win);

    if (mode == quicktiles::maximize) {
        win->control->set_quicktiling(quicktiles::none);
        if (win->maximizeMode() == maximize_mode::full) {
            set_maximize(win, false, false);
        } else {
            // set_maximize() would set moveResizeGeom as geom_restore
            auto prev_geom_restore = win->restore_geometries.maximize;
            win->control->set_quicktiling(quicktiles::maximize);
            set_maximize(win, true, true);
            auto clientArea = workspace()->clientArea(MaximizeArea, win);
            if (win->frameGeometry().top() != clientArea.top()) {
                QRect r(win->frameGeometry());
                r.moveTop(clientArea.top());
                win->setFrameGeometry(r);
            }
            win->restore_geometries.maximize = prev_geom_restore;
        }
        Q_EMIT win->quicktiling_changed();
        return;
    }

    // sanitize the mode, ie. simplify "invalid" combinations
    if ((mode & quicktiles::horizontal) == quicktiles::horizontal) {
        mode &= ~quicktiles::horizontal;
    }
    if ((mode & quicktiles::vertical) == quicktiles::vertical) {
        mode &= ~quicktiles::vertical;
    }

    // used by electric_border_maximize_geometry(.)
    win->control->set_electric(mode);

    // Restore from maximized so that it is possible to tile maximized windows with one hit or by
    // dragging.
    if (win->maximizeMode() != maximize_mode::restore) {
        if (mode != quicktiles::none) {
            // decorations may turn off some borders when tiled
            auto const geom_mode = decoration(win) ? force_geometry::yes : force_geometry::no;

            // Temporary, so the maximize code doesn't get all confused
            win->control->set_quicktiling(quicktiles::none);

            set_maximize(win, false, false);

            win->setFrameGeometry(
                electric_border_maximize_geometry(
                    win, keyboard ? win->frameGeometry().center() : Cursor::pos(), win->desktop()),
                geom_mode);
            // Store the mode change
            win->control->set_quicktiling(mode);
        } else {
            win->control->set_quicktiling(mode);
            set_maximize(win, false, false);
        }

        Q_EMIT win->quicktiling_changed();
        return;
    }

    if (mode != quicktiles::none) {
        auto whichScreen = keyboard ? win->frameGeometry().center() : Cursor::pos();

        // If trying to tile to the side that the window is already tiled to move the window to the
        // next screen if it exists, otherwise toggle the mode (set quicktiles::none)
        if (win->control->quicktiling() == mode) {
            auto const numScreens = screens()->count();
            auto const curScreen = win->screen();
            auto nextScreen = curScreen;
            QVarLengthArray<QRect> screens(numScreens);

            for (int i = 0; i < numScreens; ++i) { // Cache
                screens[i] = Screens::self()->geometry(i);
            }
            for (int i = 0; i < numScreens; ++i) {

                if (i == curScreen) {
                    continue;
                }

                if (screens[i].bottom() <= screens[curScreen].top()
                    || screens[i].top() >= screens[curScreen].bottom()) {
                    // Not in horizontal line
                    continue;
                }

                auto const x = screens[i].center().x();
                if ((mode & quicktiles::horizontal) == quicktiles::left) {
                    if (x >= screens[curScreen].center().x()
                        || (curScreen != nextScreen && x <= screens[nextScreen].center().x())) {
                        // Not left of current or more left then found next
                        continue;
                    }
                } else if ((mode & quicktiles::horizontal) == quicktiles::right) {
                    if (x <= screens[curScreen].center().x()
                        || (curScreen != nextScreen && x >= screens[nextScreen].center().x())) {
                        // Not right of current or more right then found next.
                        continue;
                    }
                }

                nextScreen = i;
            }

            if (nextScreen == curScreen) {
                mode = quicktiles::none; // No other screens, toggle tiling
            } else {
                // Move to other screen
                win->setFrameGeometry(win->restore_geometries.maximize.translated(
                    screens[nextScreen].topLeft() - screens[curScreen].topLeft()));
                whichScreen = screens[nextScreen].center();

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
            win->restore_geometries.maximize = win->frameGeometry();
        }

        if (mode != quicktiles::none) {
            win->control->set_quicktiling(mode);
            // decorations may turn off some borders when tiled
            auto const geom_mode = decoration(win) ? force_geometry::yes : force_geometry::no;
            // Temporary, so the maximize code doesn't get all confused
            win->control->set_quicktiling(quicktiles::none);
            win->setFrameGeometry(
                electric_border_maximize_geometry(win, whichScreen, win->desktop()), geom_mode);
        }

        // Store the mode change
        win->control->set_quicktiling(mode);
    }

    if (mode == quicktiles::none) {
        win->control->set_quicktiling(quicktiles::none);
        // Untiling, so just restore geometry, and we're done.
        if (!win->restore_geometries.maximize.isValid()) {
            // invalid if we started maximized and wait for placement
            win->restore_geometries.maximize = win->frameGeometry();
        }

        // decorations may turn off some borders when tiled
        auto const geom_mode = decoration(win) ? force_geometry::yes : force_geometry::no;
        win->setFrameGeometry(win->restore_geometries.maximize, geom_mode);
        // Just in case it's a different screen
        check_workspace_position(win);
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
void update_initial_move_resize_geometry(Win* win)
{
    auto& mov_res = win->control->move_resize();

    mov_res.initial_geometry = win->frameGeometry();
    mov_res.geometry = mov_res.initial_geometry;
    mov_res.start_screen = win->screen();
}

template<typename Win>
bool start_move_resize(Win* win)
{
    auto& mov_res = win->control->move_resize();

    assert(!mov_res.enabled);
    assert(QWidget::keyboardGrabber() == nullptr);
    assert(QWidget::mouseGrabber() == nullptr);

    stop_delayed_move_resize(win);

    if (QApplication::activePopupWidget() != nullptr) {
        return false; // popups have grab
    }
    if (win->control->fullscreen() && (screens()->count() < 2 || !win->isMovableAcrossScreens())) {
        return false;
    }
    if (!win->doStartMoveResize()) {
        return false;
    }

    win->control->deco().double_click.stop();

    mov_res.enabled = true;
    workspace()->setMoveResizeClient(win);

    auto const mode = mov_res.contact;

    // Means "isResize()" but moveResizeMode = true is set below
    if (mode != position::center) {
        // Partial is cond. reset in finish_move_resize
        if (win->maximizeMode() == maximize_mode::full) {
            win->restore_geometries.maximize = win->frameGeometry();
            set_maximize(win, false, false);
        }
    }

    if (win->control->quicktiling() != quicktiles::none
        && mode != position::center) { // Cannot use isResize() yet
        // Exit quick tile mode when the user attempts to resize a tiled window.
        // Do so without restoring original geometry.
        win->control->set_quicktiling(quicktiles::none);
        win->restore_geometries.maximize = win->frameGeometry();
        Q_EMIT win->quicktiling_changed();
    }

    win->control->update_have_resize_effect();
    update_initial_move_resize_geometry(win);
    check_unrestricted_move_resize(win);

    Q_EMIT win->clientStartUserMovedResized(win);

    if (ScreenEdges::self()->isDesktopSwitchingMovingClients()) {
        ScreenEdges::self()->reserveDesktopSwitching(true, Qt::Vertical | Qt::Horizontal);
    }

    return true;
}

template<typename Win>
void perform_move_resize(Win* win)
{
    auto const& geom = win->control->move_resize().geometry;

    if (is_move(win) || (is_resize(win) && !win->control->have_resize_effect())) {
        win->setFrameGeometry(geom, force_geometry::no);
    }

    win->doPerformMoveResize();
    Q_EMIT win->clientStepUserMovedResized(win, geom);
}

template<typename Win>
auto move_resize_impl(Win* win, int x, int y, int x_root, int y_root)
{
    if (win->isWaitingForMoveResizeSync()) {
        // We're still waiting for the client or the timeout.
        return;
    }

    auto& mov_res = win->control->move_resize();

    auto const mode = mov_res.contact;
    if ((mode == position::center && !win->isMovableAcrossScreens())
        || (mode != position::center && (shaded(win) || !win->isResizable()))) {
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

    // ShadeHover or ShadeActive, ShadeNormal was already avoided above
    if (mode != position::center && win->shadeMode() != win::shade::none)
        win->setShade(shade::none);

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
            QRegion availableArea(workspace()->clientArea(FullArea, -1, 0));  // On the screen
            availableArea -= workspace()->restrictedMoveArea(win->desktop()); // Strut areas
            bool transposed = false;
            int requiredPixels;
            QRect bTitleRect = titleBarRect(transposed, requiredPixels);
            int lastVisiblePixels = -1;
            auto lastTry = mov_res.geometry;
            bool titleFailed = false;

            for (;;) {
                const QRect titleRect(bTitleRect.translated(mov_res.geometry.topLeft()));
                int visiblePixels = 0;
                int realVisiblePixels = 0;
                for (auto const& rect : availableArea) {
                    const QRect r = rect & titleRect;
                    realVisiblePixels += r.width() * r.height();
                    if ((transposed && r.width() == titleRect.width())
                        || // Only the full size regions...
                        (!transposed
                         && r.height() == titleRect.height())) // ...prevents long slim areas
                        visiblePixels += r.width() * r.height();
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
        auto size = adjusted_size(win, mov_res.geometry.size(), sizeMode);
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
        if (!win->isMovable()) { // isMovableAcrossScreens() must have been true to get here
            // Special moving of maximized windows on Xinerama screens
            int screen = screens()->number(globalPos);
            if (win->control->fullscreen())
                mov_res.geometry = workspace()->clientArea(FullScreenArea, screen, 0);
            else {
                auto moveResizeGeom = workspace()->clientArea(MaximizeArea, screen, 0);
                auto adjSize = adjusted_size(win, moveResizeGeom.size(), size_mode::max);
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
                auto const strut = workspace()->restrictedMoveArea(win->desktop()); // Strut areas
                QRegion availableArea(workspace()->clientArea(FullArea, -1, 0));    // On the screen
                availableArea -= strut;                                             // Strut areas
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
                    if (screens()->count() > 1) { // optimization
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

    if (!update)
        return;

    if (is_resize(win) && !win->control->have_resize_effect()) {
        win->doResizeSync();
    } else {
        perform_move_resize(win);
    }

    if (is_move(win)) {
        ScreenEdges::self()->check(globalPos, QDateTime::fromMSecsSinceEpoch(xTime(), Qt::UTC));
    }
}

template<typename Win>
auto move_resize(Win* win, QPoint const& local, QPoint const& global)
{
    auto const old_geo = win->frameGeometry();
    auto& mov_res = win->control->move_resize();

    move_resize_impl(win, local.x(), local.y(), global.x(), global.y());

    if (!win->control->fullscreen() && is_move(win)) {

        if (win->control->quicktiling() != quicktiles::none && old_geo != win->frameGeometry()) {
            geometry_updates_blocker blocker(win);
            set_quicktile_mode(win, quicktiles::none, false);
            auto const& geom_restore = win->restore_geometries.maximize;

            mov_res.offset = QPoint(double(mov_res.offset.x()) / double(old_geo.width())
                                        * double(geom_restore.width()),
                                    double(mov_res.offset.y()) / double(old_geo.height())
                                        * double(geom_restore.height()));

            if (!win->control->rules().checkMaximize(maximize_mode::restore)) {
                mov_res.geometry = geom_restore;
            }

            // Fix position.
            move_resize_impl(win, local.x(), local.y(), global.x(), global.y());

        } else if (win->control->quicktiling() == quicktiles::none && win->isResizable()) {
            check_quicktile_maximization_zones(win, global.x(), global.y());
        }
    }
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

    // Store across leaveMoveResize
    auto const wasResize = is_resize(win);
    win->leaveMoveResize();

    auto& mov_res = win->control->move_resize();
    if (cancel) {
        win->setFrameGeometry(mov_res.initial_geometry);
    } else {
        auto const& moveResizeGeom = mov_res.geometry;
        if (wasResize) {
            auto const restoreH = win->maximizeMode() == maximize_mode::horizontal
                && moveResizeGeom.width() != mov_res.initial_geometry.width();
            auto const restoreV = win->maximizeMode() == maximize_mode::vertical
                && moveResizeGeom.height() != mov_res.initial_geometry.height();
            if (restoreH || restoreV) {
                win->changeMaximize(restoreH, restoreV, false);
            }
        }
        win->setFrameGeometry(moveResizeGeom);
    }

    // Needs to be done because clientFinishUserMovedResized has not yet re-activated online
    // alignment.
    win->checkScreen();

    if (win->screen() != mov_res.start_screen) {
        // Checks rule validity
        workspace()->sendClientToScreen(win, win->screen());
        if (win->maximizeMode() != maximize_mode::restore) {
            check_workspace_position(win);
        }
    }

    if (win->control->electric_maximizing()) {
        set_quicktile_mode(win, win->control->electric(), false);
        set_electric_maximizing(win, false);
    } else if (!cancel) {
        auto geom_restore = win->restore_geometries.maximize;
        if (!(win->maximizeMode() & maximize_mode::horizontal)) {
            geom_restore.setX(win->frameGeometry().x());
            geom_restore.setWidth(win->frameGeometry().width());
        }
        if (!(win->maximizeMode() & maximize_mode::vertical)) {
            geom_restore.setY(win->frameGeometry().y());
            geom_restore.setHeight(win->frameGeometry().height());
        }
        win->restore_geometries.maximize = geom_restore;
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
void dont_move_resize(Win* win)
{
    auto& mov_res = win->control->move_resize();

    mov_res.button_down = false;
    win::stop_delayed_move_resize(win);
    if (mov_res.enabled) {
        finish_move_resize(win, false);
    }
}

/**
 * Schedules a repaint for the visible rectangle before and after a
 * geometry update. The current visible rectangle is stored for the
 * next time this method is called as the before-geometry.
 */
template<typename Win>
void add_repaint_during_geometry_updates(Win* win)
{
    auto const deco_rect = visible_rect(win);
    win->addLayerRepaint(win->control->geometry_update.original.visible);

    // Trigger repaint of window's new location.
    win->addLayerRepaint(deco_rect);
    win->control->geometry_update.original.visible = deco_rect;
}

template<typename Win>
void move(Win* win, QPoint const& point, force_geometry force = force_geometry::no)
{
    assert(win->control->geometry_update.pending == pending_geometry::none
           || win->control->geometry_update.block);

    auto old_frame_geometry = win->frameGeometry();

    if (!win->control->prepare_move(point, force)) {
        return;
    }

    if (win->control->geometry_update.block) {
        // Only update if not already designated as being forced.
        if (win->control->geometry_update.pending != pending_geometry::forced) {
            win->control->geometry_update.pending = force == force_geometry::yes
                ? pending_geometry::forced
                : pending_geometry::normal;
        }
        return;
    }

    win->control->do_move();

    win->updateWindowRules(Rules::Position);
    screens()->setCurrent(win);
    workspace()->updateStackingOrder();

    // client itself is not damaged
    add_repaint_during_geometry_updates(win);
    win->control->update_geometry_before_update_blocking();

    Q_EMIT win->geometryChanged();
    Q_EMIT win->frameGeometryChanged(win, old_frame_geometry);
}

template<typename Win>
void keep_in_area(Win* win, QRect area, bool partial)
{
    auto pos = win->pos();
    auto size = win->size();

    if (partial) {
        // Increase the area so that can have only 100 pixels in the area.
        area.setLeft(std::min(pos.x() - size.width() + 100, area.left()));
        area.setTop(std::min(area.top() - size.height() + 100, area.top()));
        area.setRight(std::max(area.right() + size.width() - 100, area.right()));
        area.setBottom(std::max(area.bottom() + size.height() - 100, area.bottom()));
    } else if (area.width() < size.width() || area.height() < size.height()) {
        // Resize to fit into area.
        win->resizeWithChecks(
            QSize(std::min(area.width(), size.width()), std::min(area.height(), size.height())));

        pos = win->pos();
        size = win->size();
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
    workspace()->updateFocusMousePosition(Cursor::pos());

    auto const old_screen = win->screen();
    move(win, QPoint(left, top));
    if (win->screen() != old_screen) {
        // Checks rule validity.
        workspace()->sendClientToScreen(win, win->screen());
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

#endif
