/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_WIN_MOVE_H
#define KWIN_WIN_MOVE_H

#include "cursor.h"
#include "outline.h"
#include "screenedge.h"
#include "screens.h"
#include "types.h"
#include "workspace.h"

namespace KWin::win
{

template<typename Win>
class geometry_updates_blocker
{
public:
    explicit geometry_updates_blocker(Win* c)
        : cl(c)
    {
        cl->blockGeometryUpdates(true);
    }
    ~geometry_updates_blocker()
    {
        cl->blockGeometryUpdates(false);
    }

private:
    Win* cl;
};

inline int sign(int v)
{
    return (v > 0) - (v < 0);
}

/**
 * Returns @c true if @p win is being interactively moved; otherwise @c false.
 */
template<typename Win>
bool is_move(Win* win)
{
    return win->isMoveResize() && win->moveResizePointerMode_win() == position::center;
}

/**
 * Returns @c true if @p win is being interactively resized; otherwise @c false.
 */
template<typename Win>
bool is_resize(Win* win)
{
    return win->isMoveResize() && win->moveResizePointerMode_win() != position::center;
}

/**
 * Adjust the frame size @p frame according to the size hints of @p win.
 */
template<typename Win>
QSize adjusted_size(Win* win, QSize const& frame, size_mode mode)
{
    // first, get the window size for the given frame size s
    auto wsize = win->frameSizeToClientSize(frame);
    if (wsize.isEmpty()) {
        wsize = QSize(qMax(wsize.width(), 1), qMax(wsize.height(), 1));
    }

    return win->sizeForClientSize_win(wsize, mode, false);
}

/**
 * This helper returns proper size even if the window is shaded,
 * see also the comment in X11Client::setGeometry().
 */
template<typename Win>
QSize adjusted_size(Win* win)
{
    return win->sizeForClientSize(win->clientSize());
}

// This function checks if it actually makes sense to perform a restricted move/resize.
// If e.g. the titlebar is already outside of the workarea, there's no point in performing
// a restricted move resize, because then e.g. resize would also move the window (#74555).
// NOTE: Most of it is duplicated from move_resize().
template<typename Win>
void check_unrestricted_move_resize(Win* win)
{
    if (win->isUnrestrictedMoveResize()) {
        return;
    }

    auto const& moveResizeGeom = win->moveResizeGeometry();
    auto desktopArea = workspace()->clientArea(WorkArea, moveResizeGeom.center(), win->desktop());
    int left_marge, right_marge, top_marge, bottom_marge, titlebar_marge;

    // restricted move/resize - keep at least part of the titlebar always visible
    // how much must remain visible when moved away in that direction
    left_marge = qMin(100 + win->borderRight(), moveResizeGeom.width());
    right_marge = qMin(100 + win->borderLeft(), moveResizeGeom.width());

    // width/height change with opaque resizing, use the initial ones
    titlebar_marge = win->initialMoveResizeGeometry().height();
    top_marge = win->borderBottom();
    bottom_marge = win->borderTop();

    if (is_resize(win)) {
        if (moveResizeGeom.bottom() < desktopArea.top() + top_marge) {
            win->setUnrestrictedMoveResize(true);
        }
        if (moveResizeGeom.top() > desktopArea.bottom() - bottom_marge) {
            win->setUnrestrictedMoveResize(true);
        }
        if (moveResizeGeom.right() < desktopArea.left() + left_marge) {
            win->setUnrestrictedMoveResize(true);
        }
        if (moveResizeGeom.left() > desktopArea.right() - right_marge) {
            win->setUnrestrictedMoveResize(true);
        }
        if (!win->isUnrestrictedMoveResize()
            && moveResizeGeom.top() < desktopArea.top()) { // titlebar mustn't go out
            win->setUnrestrictedMoveResize(true);
        }
    }
    if (is_move(win)) {
        if (moveResizeGeom.bottom() < desktopArea.top() + titlebar_marge - 1) {
            win->setUnrestrictedMoveResize(true);
        }

        // no need to check top_marge, titlebar_marge already handles it
        if (moveResizeGeom.top()
            > desktopArea.bottom() - bottom_marge + 1) { // titlebar mustn't go out
            win->setUnrestrictedMoveResize(true);
        }
        if (moveResizeGeom.right() < desktopArea.left() + left_marge) {
            win->setUnrestrictedMoveResize(true);
        }
        if (moveResizeGeom.left() > desktopArea.right() - right_marge) {
            win->setUnrestrictedMoveResize(true);
        }
    }
}

inline void check_offscreen_position(QRect* geom, const QRect& screenArea)
{
    if (geom->left() > screenArea.right()) {
        geom->moveLeft(screenArea.right() - screenArea.width() / 4);
    } else if (geom->right() < screenArea.left()) {
        geom->moveRight(screenArea.left() + screenArea.width() / 4);
    }
    if (geom->top() > screenArea.bottom()) {
        geom->moveTop(screenArea.bottom() - screenArea.height() / 4);
    } else if (geom->bottom() < screenArea.top()) {
        geom->moveBottom(screenArea.top() + screenArea.width() / 4);
    }
}

template<typename Win>
QRect electric_border_maximize_geometry(Win const* win, QPoint pos, int desktop)
{
    if (win->electricBorderMode() == QuickTileMode(QuickTileFlag::Maximize)) {
        if (win->maximizeMode() == MaximizeFull) {
            return win->geometryRestore();
        } else {
            return workspace()->clientArea(MaximizeArea, pos, desktop);
        }
    }

    auto ret = workspace()->clientArea(MaximizeArea, pos, desktop);

    if (win->electricBorderMode() & QuickTileFlag::Left) {
        ret.setRight(ret.left() + ret.width() / 2 - 1);
    } else if (win->electricBorderMode() & QuickTileFlag::Right) {
        ret.setLeft(ret.right() - (ret.width() - ret.width() / 2) + 1);
    }

    if (win->electricBorderMode() & QuickTileFlag::Top) {
        ret.setBottom(ret.top() + ret.height() / 2 - 1);
    } else if (win->electricBorderMode() & QuickTileFlag::Bottom) {
        ret.setTop(ret.bottom() - (ret.height() - ret.height() / 2) + 1);
    }

    return ret;
}

template<typename Win>
void check_workspace_position(Win* win,
                              QRect oldGeometry = QRect(),
                              int oldDesktop = -2,
                              QRect oldClientGeometry = QRect())
{
    enum { Left = 0, Top, Right, Bottom };
    int const border[4]
        = {win->borderLeft(), win->borderTop(), win->borderRight(), win->borderBottom()};

    if (!oldGeometry.isValid()) {
        oldGeometry = win->frameGeometry();
    }
    if (oldDesktop == -2) {
        oldDesktop = win->desktop();
    }
    if (!oldClientGeometry.isValid()) {
        oldClientGeometry
            = oldGeometry.adjusted(border[Left], border[Top], -border[Right], -border[Bottom]);
    }

    if (win->isDesktop()) {
        return;
    }

    if (win->isFullScreen()) {
        auto area = workspace()->clientArea(FullScreenArea, win);
        if (win->frameGeometry() != area) {
            win->setFrameGeometry(area);
        }
        return;
    }
    if (win->isDock()) {
        return;
    }

    if (win->maximizeMode() != MaximizeRestore) {
        geometry_updates_blocker block(win);
        // Adjust size
        win->changeMaximize(false, false, true);
        const QRect screenArea = workspace()->clientArea(ScreenArea, win);
        auto geom = win->frameGeometry();
        check_offscreen_position(&geom, screenArea);
        win->setFrameGeometry(geom);
        return;
    }

    if (win->quickTileMode() != QuickTileMode(QuickTileFlag::None)) {
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
    QRect oldScreenArea;
    if (workspace()->inUpdateClientArea()) {
        // we need to find the screen area as it was before the change
        oldScreenArea
            = QRect(0, 0, workspace()->oldDisplayWidth(), workspace()->oldDisplayHeight());
        int distance = INT_MAX;
        foreach (const QRect& r, workspace()->previousScreenSizes()) {
            int d = r.contains(oldGeometry.center())
                ? 0
                : (r.center() - oldGeometry.center()).manhattanLength();
            if (d < distance) {
                distance = d;
                oldScreenArea = r;
            }
        }
    } else {
        oldScreenArea = workspace()->clientArea(ScreenArea, oldGeometry.center(), oldDesktop);
    }
    auto const oldGeomTall = QRect(oldGeometry.x(),
                                   oldScreenArea.y(),
                                   oldGeometry.width(),
                                   oldScreenArea.height()); // Full screen height
    auto const oldGeomWide = QRect(oldScreenArea.x(),
                                   oldGeometry.y(),
                                   oldScreenArea.width(),
                                   oldGeometry.height()); // Full screen width
    auto oldTopMax = oldScreenArea.y();
    auto oldRightMax = oldScreenArea.x() + oldScreenArea.width();
    auto oldBottomMax = oldScreenArea.y() + oldScreenArea.height();
    auto oldLeftMax = oldScreenArea.x();
    auto const screenArea
        = workspace()->clientArea(ScreenArea, win->geometryRestore().center(), win->desktop());
    auto topMax = screenArea.y();
    auto rightMax = screenArea.x() + screenArea.width();
    auto bottomMax = screenArea.y() + screenArea.height();
    auto leftMax = screenArea.x();
    auto newGeom = win->geometryRestore();
    auto newClientGeom
        = newGeom.adjusted(border[Left], border[Top], -border[Right], -border[Bottom]);

    // Full screen height
    auto const newGeomTall
        = QRect(newGeom.x(), screenArea.y(), newGeom.width(), screenArea.height());
    // Full screen width
    auto const newGeomWide
        = QRect(screenArea.x(), newGeom.y(), screenArea.width(), newGeom.height());

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

    for (const QRect& r : (workspace()->*moveAreaFunc)(oldDesktop, StrutAreaTop)) {
        QRect rect = r & oldGeomTall;
        if (!rect.isEmpty()) {
            oldTopMax = qMax(oldTopMax, rect.y() + rect.height());
        }
    }
    for (const QRect& r : (workspace()->*moveAreaFunc)(oldDesktop, StrutAreaRight)) {
        QRect rect = r & oldGeomWide;
        if (!rect.isEmpty()) {
            oldRightMax = qMin(oldRightMax, rect.x());
        }
    }
    for (const QRect& r : (workspace()->*moveAreaFunc)(oldDesktop, StrutAreaBottom)) {
        QRect rect = r & oldGeomTall;
        if (!rect.isEmpty()) {
            oldBottomMax = qMin(oldBottomMax, rect.y());
        }
    }
    for (const QRect& r : (workspace()->*moveAreaFunc)(oldDesktop, StrutAreaLeft)) {
        QRect rect = r & oldGeomWide;
        if (!rect.isEmpty()) {
            oldLeftMax = qMax(oldLeftMax, rect.x() + rect.width());
        }
    }

    // These 4 compute new bounds
    for (const QRect& r : workspace()->restrictedMoveArea(win->desktop(), StrutAreaTop)) {
        QRect rect = r & newGeomTall;
        if (!rect.isEmpty()) {
            topMax = qMax(topMax, rect.y() + rect.height());
        }
    }
    for (const QRect& r : workspace()->restrictedMoveArea(win->desktop(), StrutAreaRight)) {
        QRect rect = r & newGeomWide;
        if (!rect.isEmpty()) {
            rightMax = qMin(rightMax, rect.x());
        }
    }
    for (const QRect& r : workspace()->restrictedMoveArea(win->desktop(), StrutAreaBottom)) {
        QRect rect = r & newGeomTall;
        if (!rect.isEmpty()) {
            bottomMax = qMin(bottomMax, rect.y());
        }
    }
    for (const QRect& r : workspace()->restrictedMoveArea(win->desktop(), StrutAreaLeft)) {
        QRect rect = r & newGeomWide;
        if (!rect.isEmpty()) {
            leftMax = qMax(leftMax, rect.x() + rect.width());
        }
    }

    // Check if the sides were inside or touching but are no longer
    bool keep[4] = {false, false, false, false};
    bool save[4] = {false, false, false, false};
    int padding[4] = {0, 0, 0, 0};
    if (oldGeometry.x() >= oldLeftMax) {
        save[Left] = newGeom.x() < leftMax;
    }
    if (oldGeometry.x() == oldLeftMax) {
        keep[Left] = newGeom.x() != leftMax;
    } else if (oldClientGeometry.x() == oldLeftMax && newClientGeom.x() != leftMax) {
        padding[0] = border[Left];
        keep[Left] = true;
    }
    if (oldGeometry.y() >= oldTopMax) {
        save[Top] = newGeom.y() < topMax;
    }
    if (oldGeometry.y() == oldTopMax) {
        keep[Top] = newGeom.y() != topMax;
    } else if (oldClientGeometry.y() == oldTopMax && newClientGeom.y() != topMax) {
        padding[1] = border[Left];
        keep[Top] = true;
    }
    if (oldGeometry.right() <= oldRightMax - 1) {
        save[Right] = newGeom.right() > rightMax - 1;
    }
    if (oldGeometry.right() == oldRightMax - 1) {
        keep[Right] = newGeom.right() != rightMax - 1;
    } else if (oldClientGeometry.right() == oldRightMax - 1
               && newClientGeom.right() != rightMax - 1) {
        padding[2] = border[Right];
        keep[Right] = true;
    }
    if (oldGeometry.bottom() <= oldBottomMax - 1) {
        save[Bottom] = newGeom.bottom() > bottomMax - 1;
    }
    if (oldGeometry.bottom() == oldBottomMax - 1) {
        keep[Bottom] = newGeom.bottom() != bottomMax - 1;
    } else if (oldClientGeometry.bottom() == oldBottomMax - 1
               && newClientGeom.bottom() != bottomMax - 1) {
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
        newGeom.moveLeft(qMax(leftMax, screenArea.x()) - padding[0]);
    }
    if (padding[0] && screens()->intersecting(newGeom) > 1) {
        newGeom.moveLeft(newGeom.left() + padding[0]);
    }
    if (save[Top] || keep[Top]) {
        newGeom.moveTop(qMax(topMax, screenArea.y()) - padding[1]);
    }
    if (padding[1] && screens()->intersecting(newGeom) > 1) {
        newGeom.moveTop(newGeom.top() + padding[1]);
    }
    if (save[Right] || keep[Right]) {
        newGeom.moveRight(qMin(rightMax - 1, screenArea.right()) + padding[2]);
    }
    if (padding[2] && screens()->intersecting(newGeom) > 1) {
        newGeom.moveRight(newGeom.right() - padding[2]);
    }
    if (oldGeometry.x() >= oldLeftMax && newGeom.x() < leftMax) {
        newGeom.setLeft(qMax(leftMax, screenArea.x()));
    } else if (oldClientGeometry.x() >= oldLeftMax && newGeom.x() + border[Left] < leftMax) {
        newGeom.setLeft(qMax(leftMax, screenArea.x()) - border[Left]);
        if (screens()->intersecting(newGeom) > 1) {
            newGeom.setLeft(newGeom.left() + border[Left]);
        }
    }
    if (save[Bottom] || keep[Bottom]) {
        newGeom.moveBottom(qMin(bottomMax - 1, screenArea.bottom()) + padding[3]);
    }
    if (padding[3] && screens()->intersecting(newGeom) > 1) {
        newGeom.moveBottom(newGeom.bottom() - padding[3]);
    }

    if (oldGeometry.y() >= oldTopMax && newGeom.y() < topMax) {
        newGeom.setTop(qMax(topMax, screenArea.y()));
    } else if (oldClientGeometry.y() >= oldTopMax && newGeom.y() + border[Top] < topMax) {
        newGeom.setTop(qMax(topMax, screenArea.y()) - border[Top]);
        if (screens()->intersecting(newGeom) > 1) {
            newGeom.setTop(newGeom.top() + border[Top]);
        }
    }

    check_offscreen_position(&newGeom, screenArea);

    // Obey size hints. TODO: We really should make sure it stays in the right place
    if (!win->isShade()) {
        newGeom.setSize(adjusted_size(win, newGeom.size(), size_mode::any));
    }
    if (newGeom != win->frameGeometry()) {
        win->setFrameGeometry(newGeom);
    }
}

template<typename Win>
void set_maximize(Win* win, bool vertically, bool horizontally)
{
    // set_maximize() flips the state, so change from set->flip
    auto const oldMode = win->maximizeMode_win();
    win->changeMaximize(
        (oldMode & maximize_mode::horizontal) == maximize_mode::horizontal ? !horizontally
                                                                           : horizontally,
        (oldMode & maximize_mode::vertical) == maximize_mode::vertical ? !vertically : vertically,
        false);
    auto const newMode = win->maximizeMode_win();
    if (oldMode != newMode) {
        win->clientMaximizedStateChanged_win(newMode);
        Q_EMIT win->clientMaximizedStateChanged(win, vertically, horizontally);
    }
}

template<typename Win>
void maximize(Win* win, maximize_mode mode)
{
    set_maximize(win,
                 (mode & maximize_mode::vertical) == maximize_mode::vertical,
                 (mode & maximize_mode::horizontal) == maximize_mode::horizontal);
}

/**
 * Sets the quick tile mode ("snap") of this window.
 * This will also handle preserving and restoring of window geometry as necessary.
 * @param mode The tile mode (left/right) to give this window.
 * @param keyboard Defines whether to take keyboard cursor into account.
 */
template<typename Win>
void set_quicktile_mode(Win* win, QuickTileMode mode, bool keyboard)
{
    // Only allow quick tile on a regular window.
    if (!win->isResizable()) {
        return;
    }

    // May cause leave event
    workspace()->updateFocusMousePosition(Cursor::pos());

    geometry_updates_blocker blocker(win);

    if (mode == QuickTileMode(QuickTileFlag::Maximize)) {
        win->set_QuickTileMode_win(QuickTileFlag::None);
        if (win->maximizeMode() == MaximizeFull) {
            set_maximize(win, false, false);
        } else {
            // set_maximize() would set moveResizeGeom as geom_restore
            auto prev_geom_restore = win->geometryRestore();
            win->set_QuickTileMode_win(QuickTileFlag::Maximize);
            set_maximize(win, true, true);
            auto clientArea = workspace()->clientArea(MaximizeArea, win);
            if (win->frameGeometry().top() != clientArea.top()) {
                QRect r(win->frameGeometry());
                r.moveTop(clientArea.top());
                win->setFrameGeometry(r);
            }
            win->setGeometryRestore(prev_geom_restore);
        }
        Q_EMIT win->quickTileModeChanged();
        return;
    }

    // sanitize the mode, ie. simplify "invalid" combinations
    if ((mode & QuickTileFlag::Horizontal) == QuickTileMode(QuickTileFlag::Horizontal)) {
        mode &= ~QuickTileMode(QuickTileFlag::Horizontal);
    }
    if ((mode & QuickTileFlag::Vertical) == QuickTileMode(QuickTileFlag::Vertical)) {
        mode &= ~QuickTileMode(QuickTileFlag::Vertical);
    }

    // used by electric_border_maximize_geometry(.)
    win->setElectricBorderMode(mode);

    // Restore from maximized so that it is possible to tile maximized windows with one hit or by
    // dragging.
    if (win->maximizeMode() != MaximizeRestore) {
        if (mode != QuickTileMode(QuickTileFlag::None)) {
            // decorations may turn off some borders when tiled
            auto const geom_mode = win->isDecorated() ? force_geometry::yes : force_geometry::no;

            // Temporary, so the maximize code doesn't get all confused
            win->set_QuickTileMode_win(QuickTileFlag::None);

            set_maximize(win, false, false);

            win->setFrameGeometry_win(
                electric_border_maximize_geometry(
                    win, keyboard ? win->frameGeometry().center() : Cursor::pos(), win->desktop()),
                geom_mode);
            // Store the mode change
            win->set_QuickTileMode_win(mode);
        } else {
            win->set_QuickTileMode_win(mode);
            set_maximize(win, false, false);
        }

        Q_EMIT win->quickTileModeChanged();
        return;
    }

    if (mode != QuickTileMode(QuickTileFlag::None)) {
        auto whichScreen = keyboard ? win->frameGeometry().center() : Cursor::pos();

        // If trying to tile to the side that the window is already tiled to move the window to the
        // next screen if it exists, otherwise toggle the mode (set QuickTileFlag::None)
        if (win->quickTileMode() == mode) {
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
                if ((mode & QuickTileFlag::Horizontal) == QuickTileMode(QuickTileFlag::Left)) {
                    if (x >= screens[curScreen].center().x()
                        || (curScreen != nextScreen && x <= screens[nextScreen].center().x())) {
                        // Not left of current or more left then found next
                        continue;
                    }
                } else if ((mode & QuickTileFlag::Horizontal)
                           == QuickTileMode(QuickTileFlag::Right)) {
                    if (x <= screens[curScreen].center().x()
                        || (curScreen != nextScreen && x >= screens[nextScreen].center().x())) {
                        // Not right of current or more right then found next.
                        continue;
                    }
                }

                nextScreen = i;
            }

            if (nextScreen == curScreen) {
                mode = QuickTileFlag::None; // No other screens, toggle tiling
            } else {
                // Move to other screen
                win->setFrameGeometry(win->geometryRestore().translated(
                    screens[nextScreen].topLeft() - screens[curScreen].topLeft()));
                whichScreen = screens[nextScreen].center();

                // Swap sides
                if (mode & QuickTileFlag::Horizontal) {
                    mode = (~mode & QuickTileFlag::Horizontal) | (mode & QuickTileFlag::Vertical);
                }
            }
            // used by electric_border_maximize_geometry(.)
            win->setElectricBorderMode(mode);
        } else if (win->quickTileMode() == QuickTileMode(QuickTileFlag::None)) {
            // Not coming out of an existing tile, not shifting monitors, we're setting a brand new
            // tile. Store geometry first, so we can go out of this tile later.
            win->setGeometryRestore(win->frameGeometry());
        }

        if (mode != QuickTileMode(QuickTileFlag::None)) {
            win->set_QuickTileMode_win(mode);
            // decorations may turn off some borders when tiled
            auto const geom_mode = win->isDecorated() ? force_geometry::yes : force_geometry::no;
            // Temporary, so the maximize code doesn't get all confused
            win->set_QuickTileMode_win(QuickTileFlag::None);
            win->setFrameGeometry_win(
                electric_border_maximize_geometry(win, whichScreen, win->desktop()), geom_mode);
        }

        // Store the mode change
        win->set_QuickTileMode_win(mode);
    }

    if (mode == QuickTileMode(QuickTileFlag::None)) {
        win->set_QuickTileMode_win(QuickTileFlag::None);
        // Untiling, so just restore geometry, and we're done.
        if (!win->geometryRestore().isValid()) {
            // invalid if we started maximized and wait for placement
            win->setGeometryRestore(win->frameGeometry());
        }

        // decorations may turn off some borders when tiled
        auto const geom_mode = win->isDecorated() ? force_geometry::yes : force_geometry::no;
        win->setFrameGeometry_win(win->geometryRestore(), geom_mode);
        // Just in case it's a different screen
        check_workspace_position(win);
    }
    Q_EMIT win->quickTileModeChanged();
}

template<typename Win>
bool start_move_resize(Win* win)
{
    assert(!win->isMoveResize());
    assert(QWidget::keyboardGrabber() == nullptr);
    assert(QWidget::mouseGrabber() == nullptr);

    win->stopDelayedMoveResize();

    if (QApplication::activePopupWidget() != nullptr) {
        return false; // popups have grab
    }
    if (win->isFullScreen() && (screens()->count() < 2 || !win->isMovableAcrossScreens())) {
        return false;
    }
    if (!win->doStartMoveResize()) {
        return false;
    }

    win->invalidateDecorationDoubleClickTimer();

    win->setMoveResize(true);
    workspace()->setMoveResizeClient(win);

    auto const mode = win->moveResizePointerMode_win();

    // Means "isResize()" but moveResizeMode = true is set below
    if (mode != position::center) {
        // Partial is cond. reset in finish_move_resize
        if (win->maximizeMode_win() == maximize_mode::full) {
            win->setGeometryRestore(win->frameGeometry()); // "restore" to current geometry
            set_maximize(win, false, false);
        }
    }

    if (win->quickTileMode() != QuickTileMode(QuickTileFlag::None)
        && mode != position::center) { // Cannot use isResize() yet
        // Exit quick tile mode when the user attempts to resize a tiled window
        win->updateQuickTileMode(QuickTileFlag::None); // Do so without restoring original geometry
        win->setGeometryRestore(win->frameGeometry());
        Q_EMIT win->quickTileModeChanged();
    }

    win->updateHaveResizeEffect();
    win->updateInitialMoveResizeGeometry();
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
    auto const& geom = win->moveResizeGeometry();

    if (is_move(win) || (is_resize(win) && !win->haveResizeEffect())) {
        win->setFrameGeometry_win(geom, force_geometry::no);
    }

    win->doPerformMoveResize();
    win->positionGeometryTip();
    Q_EMIT win->clientStepUserMovedResized(win, geom);
}

template<typename Win>
auto move_resize(Win* win, int x, int y, int x_root, int y_root)
{
    if (win->isWaitingForMoveResizeSync())
        return; // we're still waiting for the client or the timeout

    auto const mode = win->moveResizePointerMode_win();
    if ((mode == position::center && !win->isMovableAcrossScreens())
        || (mode != position::center && (win->isShade() || !win->isResizable())))
        return;

    if (!win->isMoveResize()) {
        QPoint p(QPoint(x /* - padding_left*/, y /* - padding_top*/) - win->moveOffset());
        if (p.manhattanLength() >= QApplication::startDragDistance()) {
            if (!start_move_resize(win)) {
                win->setMoveResizePointerButtonDown(false);
                win->updateCursor();
                return;
            }
            win->updateCursor();
        } else
            return;
    }

    // ShadeHover or ShadeActive, ShadeNormal was already avoided above
    if (mode != position::center && win->shadeMode() != ShadeNone)
        win->setShade(ShadeNone);

    QPoint globalPos(x_root, y_root);
    // these two points limit the geometry rectangle, i.e. if bottomleft resizing is done,
    // the bottomleft corner should be at is at (topleft.x(), bottomright().y())
    auto topleft = globalPos - win->moveOffset();
    auto bottomright = globalPos + win->invertedMoveOffset();
    auto previousMoveResizeGeom = win->moveResizeGeometry();

    // TODO move whole group when moving its leader or when the leader is not mapped?

    auto titleBarRect = [&win](bool& transposed, int& requiredPixels) -> QRect {
        const QRect& moveResizeGeom = win->moveResizeGeometry();
        QRect r(moveResizeGeom);
        r.moveTopLeft(QPoint(0, 0));
        switch (win->titlebarPosition_win()) {
        default:
        case position::top:
            r.setHeight(win->borderTop());
            break;
        case position::left:
            r.setWidth(win->borderLeft());
            transposed = true;
            break;
        case position::bottom:
            r.setTop(r.bottom() - win->borderBottom());
            break;
        case position::right:
            r.setLeft(r.right() - win->borderRight());
            transposed = true;
            break;
        }
        // When doing a restricted move we must always keep 100px of the titlebar
        // visible to allow the user to be able to move it again.
        requiredPixels = qMin(100 * (transposed ? r.width() : r.height()),
                              moveResizeGeom.width() * moveResizeGeom.height());
        return r;
    };

    bool update = false;
    if (is_resize(win)) {
        auto orig = win->initialMoveResizeGeometry();
        auto sizeMode = size_mode::any;
        auto calculateMoveResizeGeom = [&win, &topleft, &bottomright, &orig, &sizeMode, &mode]() {
            switch (mode) {
            case position::top_left:
                win->setMoveResizeGeometry(QRect(topleft, orig.bottomRight()));
                break;
            case position::bottom_right:
                win->setMoveResizeGeometry(QRect(orig.topLeft(), bottomright));
                break;
            case position::bottom_left:
                win->setMoveResizeGeometry(
                    QRect(QPoint(topleft.x(), orig.y()), QPoint(orig.right(), bottomright.y())));
                break;
            case position::top_right:
                win->setMoveResizeGeometry(
                    QRect(QPoint(orig.x(), topleft.y()), QPoint(bottomright.x(), orig.bottom())));
                break;
            case position::top:
                win->setMoveResizeGeometry(
                    QRect(QPoint(orig.left(), topleft.y()), orig.bottomRight()));
                sizeMode = size_mode::fixed_height; // try not to affect height
                break;
            case position::bottom:
                win->setMoveResizeGeometry(
                    QRect(orig.topLeft(), QPoint(orig.right(), bottomright.y())));
                sizeMode = size_mode::fixed_height;
                break;
            case position::left:
                win->setMoveResizeGeometry(
                    QRect(QPoint(topleft.x(), orig.top()), orig.bottomRight()));
                sizeMode = size_mode::fixed_width;
                break;
            case position::right:
                win->setMoveResizeGeometry(
                    QRect(orig.topLeft(), QPoint(bottomright.x(), orig.bottom())));
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
        win->setMoveResizeGeometry(
            workspace()->adjustClientSize(win, win->moveResizeGeometry(), static_cast<int>(mode)));

        if (!win->isUnrestrictedMoveResize()) {
            // Make sure the titlebar isn't behind a restricted area. We don't need to restrict
            // the other directions. If not visible enough, move the window to the closest valid
            // point. We bruteforce this by slowly moving the window back to its previous position
            QRegion availableArea(workspace()->clientArea(FullArea, -1, 0));  // On the screen
            availableArea -= workspace()->restrictedMoveArea(win->desktop()); // Strut areas
            bool transposed = false;
            int requiredPixels;
            QRect bTitleRect = titleBarRect(transposed, requiredPixels);
            int lastVisiblePixels = -1;
            auto lastTry = win->moveResizeGeometry();
            bool titleFailed = false;

            for (;;) {
                const QRect titleRect(bTitleRect.translated(win->moveResizeGeometry().topLeft()));
                int visiblePixels = 0;
                int realVisiblePixels = 0;
                for (const QRect& rect : availableArea) {
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
                        if (!titleFailed)
                            win->setMoveResizeGeometry(lastTry);
                        titleFailed = true;
                    }
                }
                lastVisiblePixels = realVisiblePixels;
                auto moveResizeGeom = win->moveResizeGeometry();
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
                switch (win->titlebarPosition_win()) {
                default:
                case position::top:
                    fixChangedState(topChanged, btmChanged, leftChanged, rightChanged);
                    break;
                case position::left:
                    fixChangedState(leftChanged, rightChanged, topChanged, btmChanged);
                    break;
                case position::bottom:
                    fixChangedState(btmChanged, topChanged, leftChanged, rightChanged);
                    break;
                case position::right:
                    fixChangedState(rightChanged, leftChanged, topChanged, btmChanged);
                    break;
                }
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
                win->setMoveResizeGeometry(moveResizeGeom);
            }
        }

        // Always obey size hints, even when in "unrestricted" mode
        auto size = adjusted_size(win, win->moveResizeGeometry().size(), sizeMode);
        // the new topleft and bottomright corners (after checking size constrains), if they'll be
        // needed
        topleft = QPoint(win->moveResizeGeometry().right() - size.width() + 1,
                         win->moveResizeGeometry().bottom() - size.height() + 1);
        bottomright = QPoint(win->moveResizeGeometry().left() + size.width() - 1,
                             win->moveResizeGeometry().top() + size.height() - 1);
        orig = win->moveResizeGeometry();

        // if aspect ratios are specified, both dimensions may change.
        // Therefore grow to the right/bottom if needed.
        // TODO it should probably obey gravity rather than always using right/bottom ?
        if (sizeMode == size_mode::fixed_height) {
            orig.setRight(bottomright.x());
        } else if (sizeMode == size_mode::fixed_width) {
            orig.setBottom(bottomright.y());
        }

        calculateMoveResizeGeom();

        if (win->moveResizeGeometry().size() != previousMoveResizeGeom.size()) {
            update = true;
        }
    } else if (is_move(win)) {
        Q_ASSERT(mode == position::center);
        if (!win->isMovable()) { // isMovableAcrossScreens() must have been true to get here
            // Special moving of maximized windows on Xinerama screens
            int screen = screens()->number(globalPos);
            if (win->isFullScreen())
                win->setMoveResizeGeometry(workspace()->clientArea(FullScreenArea, screen, 0));
            else {
                auto moveResizeGeom = workspace()->clientArea(MaximizeArea, screen, 0);
                auto adjSize = adjusted_size(win, moveResizeGeom.size(), size_mode::max);
                if (adjSize != moveResizeGeom.size()) {
                    QRect r(moveResizeGeom);
                    moveResizeGeom.setSize(adjSize);
                    moveResizeGeom.moveCenter(r.center());
                }
                win->setMoveResizeGeometry(moveResizeGeom);
            }
        } else {
            // first move, then snap, then check bounds
            auto moveResizeGeom = win->moveResizeGeometry();
            moveResizeGeom.moveTopLeft(topleft);
            moveResizeGeom.moveTopLeft(workspace()->adjustClientPosition(
                win, moveResizeGeom.topLeft(), win->isUnrestrictedMoveResize()));
            win->setMoveResizeGeometry(moveResizeGeom);

            if (!win->isUnrestrictedMoveResize()) {
                auto const strut = workspace()->restrictedMoveArea(win->desktop()); // Strut areas
                QRegion availableArea(workspace()->clientArea(FullArea, -1, 0));    // On the screen
                availableArea -= strut;                                             // Strut areas
                bool transposed = false;
                int requiredPixels;
                QRect bTitleRect = titleBarRect(transposed, requiredPixels);
                for (;;) {
                    auto moveResizeGeom = win->moveResizeGeometry();
                    const QRect titleRect(bTitleRect.translated(moveResizeGeom.topLeft()));
                    int visiblePixels = 0;
                    for (const QRect& rect : availableArea) {
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
                        for (const QRect& r : strut) {
                            if (r.top() == 0 && r.width() > r.height() && // "top panel"
                                r.intersects(moveResizeGeom) && moveResizeGeom.top() < r.bottom()) {
                                newTitleTop = r.bottom() + 1;
                                break;
                            }
                        }
                        if (newTitleTop > -1) {
                            moveResizeGeom.moveTop(
                                newTitleTop); // invalid position, possibly on screen change
                            win->setMoveResizeGeometry(moveResizeGeom);
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
                    win->setMoveResizeGeometry(moveResizeGeom);

                    if (moveResizeGeom == previousMoveResizeGeom) {
                        break; // Prevent lockup
                    }
                }
            }
        }
        if (win->moveResizeGeometry().topLeft() != previousMoveResizeGeom.topLeft())
            update = true;
    } else
        abort();

    if (!update)
        return;

    if (is_resize(win) && !win->haveResizeEffect()) {
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
    move_resize(win, local.x(), local.y(), global.x(), global.y());
    if (!win->isFullScreen() && is_move(win)) {

        if (win->quickTileMode() != QuickTileMode(QuickTileFlag::None)
            && old_geo != win->frameGeometry()) {
            geometry_updates_blocker blocker(win);
            set_quicktile_mode(win, QuickTileFlag::None, false);
            auto const& geom_restore = win->geometryRestore();
            win->setMoveOffset(QPoint(double(win->moveOffset().x()) / double(old_geo.width())
                                          * double(geom_restore.width()),
                                      double(win->moveOffset().y()) / double(old_geo.height())
                                          * double(geom_restore.height())));
#ifndef KWIN_UNIT_TEST
            if (win->rules()->checkMaximize(MaximizeRestore) == MaximizeRestore) {
                win->setMoveResizeGeometry(geom_restore);
            }
#endif

            // Fix position.
            move_resize(win, local.x(), local.y(), global.x(), global.y());

        } else if (win->quickTileMode() == QuickTileMode(QuickTileFlag::None)
                   && win->isResizable()) {
            win->checkQuickTilingMaximizationZones(global.x(), global.y());
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

    if (cancel) {
        win->setFrameGeometry(win->initialMoveResizeGeometry());
    } else {
        auto const& moveResizeGeom = win->moveResizeGeometry();
        if (wasResize) {
            auto const restoreH = win->maximizeMode_win() == maximize_mode::horizontal
                && moveResizeGeom.width() != win->initialMoveResizeGeometry().width();
            auto const restoreV = win->maximizeMode_win() == maximize_mode::vertical
                && moveResizeGeom.height() != win->initialMoveResizeGeometry().height();
            if (restoreH || restoreV) {
                win->changeMaximize(restoreH, restoreV, false);
            }
        }
        win->setFrameGeometry(moveResizeGeom);
    }

    // Needs to be done because clientFinishUserMovedResized has not yet re-activated online
    // alignment.
    win->checkScreen();

    if (win->screen() != win->moveResizeStartScreen()) {
        // Checks rule validity
        workspace()->sendClientToScreen(win, win->screen());
        if (win->maximizeMode() != MaximizeRestore) {
            check_workspace_position(win);
        }
    }

    if (win->isElectricBorderMaximizing()) {
        set_quicktile_mode(win, win->electricBorderMode(), false);
        win->setElectricBorderMaximizing(false);
    } else if (!cancel) {
        auto geom_restore = win->geometryRestore();
        if ((win->maximizeMode_win() & maximize_mode::horizontal) == maximize_mode::restore) {
            geom_restore.setX(win->frameGeometry().x());
            geom_restore.setWidth(win->frameGeometry().width());
        }
        if ((win->maximizeMode_win() & maximize_mode::vertical) == maximize_mode::restore) {
            geom_restore.setY(win->frameGeometry().y());
            geom_restore.setHeight(win->frameGeometry().height());
        }
        win->setGeometryRestore(geom_restore);
    }

    // FRAME    update();
    Q_EMIT win->clientFinishUserMovedResized(win);
}

template<typename Win>
void end_move_resize(Win* win)
{
    win->setMoveResizePointerButtonDown(false);
    win->stopDelayedMoveResize();
    if (win->isMoveResize()) {
        finish_move_resize(win, false);
        win->setMoveResizePointerMode(win->mousePosition());
    }
    win->updateCursor();
}

template<typename Win>
void dont_move_resize(Win* win)
{
    win->setMoveResizePointerButtonDown(false);
    win->stopDelayedMoveResize();
    if (win->isMoveResize()) {
        finish_move_resize(win, false);
    }
}

template<typename Win>
void keep_in_area(Win* win, QRect area, bool partial)
{
    if (partial) {
        // Increase the area so that can have only 100 pixels in the area.
        area.setLeft(qMin(area.left() - win->width() + 100, area.left()));
        area.setTop(qMin(area.top() - win->height() + 100, area.top()));
        area.setRight(qMax(area.right() + win->width() - 100, area.right()));
        area.setBottom(qMax(area.bottom() + win->height() - 100, area.bottom()));
    } else if (area.width() < win->width() || area.height() < win->height()) {
        // Resize to fit into area.
        win->resizeWithChecks(qMin(area.width(), win->width()), qMin(area.height(), win->height()));
    }

    auto tx = win->x();
    auto ty = win->y();

    if (win->frameGeometry().right() > area.right() && win->width() <= area.width()) {
        tx = area.right() - win->width() + 1;
    }
    if (win->frameGeometry().bottom() > area.bottom() && win->height() <= area.height()) {
        ty = area.bottom() - win->height() + 1;
    }
    if (!area.contains(win->frameGeometry().topLeft())) {
        if (tx < area.x()) {
            tx = area.x();
        }
        if (ty < area.y()) {
            ty = area.y();
        }
    }
    if (tx != win->x() || ty != win->y()) {
        win->move(tx, ty);
    }
}

}

#endif
