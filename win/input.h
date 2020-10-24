/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_WIN_INPUT_H
#define KWIN_WIN_INPUT_H

#include "abstract_client.h"
#include "control.h"
#include "move.h"
#include "net.h"
#include "options.h"
#include "stacking.h"
#include "types.h"
#include "useractions.h"
#include "workspace.h"

#include <QMouseEvent>

namespace KWin::win
{

template<typename Win>
bool is_most_recently_raised(Win* win)
{
    // The last toplevel in the unconstrained stacking order is the most recently raised one.
    auto last = workspace()->topClientOnDesktop(
        VirtualDesktopManager::self()->current(), -1, true, false);
    return last == win;
}

template<typename Win>
void key_press_event(Win* win, uint key_code)
{
    if (!is_move(win) && !is_resize(win)) {
        return;
    }

    auto is_control = key_code & Qt::CTRL;
    auto is_alt = key_code & Qt::ALT;

    key_code = key_code & ~Qt::KeyboardModifierMask;

    auto delta = is_control ? 1 : is_alt ? 32 : 8;
    auto pos = Cursor::pos();

    switch (key_code) {
    case Qt::Key_Left:
        pos.rx() -= delta;
        break;
    case Qt::Key_Right:
        pos.rx() += delta;
        break;
    case Qt::Key_Up:
        pos.ry() -= delta;
        break;
    case Qt::Key_Down:
        pos.ry() += delta;
        break;
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        win->setMoveResizePointerButtonDown(false);
        finish_move_resize(win, false);
        win->updateCursor();
        break;
    case Qt::Key_Escape:
        win->setMoveResizePointerButtonDown(false);
        finish_move_resize(win, true);
        win->updateCursor();
        break;
    default:
        return;
    }
    Cursor::setPos(pos);
}

template<typename Win>
bool perform_mouse_command(Win* win, Options::MouseCommand cmd, QPoint const& globalPos)
{
    bool replay = false;
    switch (cmd) {
    case Options::MouseRaise:
        workspace()->raiseClient(win);
        break;
    case Options::MouseLower: {
        workspace()->lowerClient(win);
        // Used to be activateNextClient(win), then topClientOnDesktop
        // since win is a mouseOp it's however safe to use the client under the mouse instead.
        if (win->control()->active() && options->focusPolicyIsReasonable()) {
            auto next = workspace()->clientUnderMouse(win->screen());
            if (next && next != win)
                workspace()->requestFocus(next, false);
        }
        break;
    }
    case Options::MouseOperationsMenu:
        if (win->control()->active() && options->isClickRaise()) {
            auto_raise(win);
        }
        workspace()->showWindowMenu(QRect(globalPos, globalPos), win);
        break;
    case Options::MouseToggleRaiseAndLower:
        workspace()->raiseOrLowerClient(win);
        break;
    case Options::MouseActivateAndRaise: {
        // For clickraise mode.
        replay = win->control()->active();
        bool mustReplay = !win->rules()->checkAcceptFocus(win->acceptsFocus());

        if (mustReplay) {
            auto it = workspace()->stackingOrder().constEnd(),
                 begin = workspace()->stackingOrder().constBegin();
            while (mustReplay && --it != begin && *it != win) {
                auto c = qobject_cast<AbstractClient*>(*it);
                if (!c || (c->control()->keep_above() && !win->control()->keep_above())
                    || (win->control()->keep_below() && !c->control()->keep_below())) {
                    // Can never raise above "it".
                    continue;
                }
                mustReplay = !(c->isOnCurrentDesktop() && c->isOnCurrentActivity()
                               && c->frameGeometry().intersects(win->frameGeometry()));
            }
        }

        workspace()->takeActivity_win(win, activation::focus | activation::raise);
        screens()->setCurrent(globalPos);
        replay = replay || mustReplay;
        break;
    }
    case Options::MouseActivateAndLower:
        workspace()->requestFocus(win);
        workspace()->lowerClient(win);
        screens()->setCurrent(globalPos);
        replay = replay || !win->rules()->checkAcceptFocus(win->acceptsFocus());
        break;
    case Options::MouseActivate:
        // For clickraise mode.
        replay = win->control()->active();
        workspace()->takeActivity_win(win, activation::focus);
        screens()->setCurrent(globalPos);
        replay = replay || !win->rules()->checkAcceptFocus(win->acceptsFocus());
        break;
    case Options::MouseActivateRaiseAndPassClick:
        workspace()->takeActivity_win(win, activation::focus | activation::raise);
        screens()->setCurrent(globalPos);
        replay = true;
        break;
    case Options::MouseActivateAndPassClick:
        workspace()->takeActivity_win(win, activation::focus);
        screens()->setCurrent(globalPos);
        replay = true;
        break;
    case Options::MouseMaximize:
        maximize(win, maximize_mode::full);
        break;
    case Options::MouseRestore:
        maximize(win, maximize_mode::restore);
        break;
    case Options::MouseMinimize:
        set_minimized(win, true);
        break;
    case Options::MouseAbove: {
        StackingUpdatesBlocker blocker(workspace());
        if (win->control()->keep_below()) {
            set_keep_below(win, false);
        } else {
            set_keep_above(win, true);
        }
        break;
    }
    case Options::MouseBelow: {
        StackingUpdatesBlocker blocker(workspace());
        if (win->control()->keep_above()) {
            set_keep_above(win, false);
        } else {
            set_keep_below(win, true);
        }
        break;
    }
    case Options::MousePreviousDesktop:
        workspace()->windowToPreviousDesktop(win);
        break;
    case Options::MouseNextDesktop:
        workspace()->windowToNextDesktop(win);
        break;
    case Options::MouseOpacityMore:
        // No point in changing the opacity of the desktop.
        if (!is_desktop(win)) {
            win->setOpacity(qMin(win->opacity() + 0.1, 1.0));
        }
        break;
    case Options::MouseOpacityLess:
        if (!is_desktop(win)) {
            win->setOpacity(qMax(win->opacity() - 0.1, 0.1));
        }
        break;
    case Options::MouseClose:
        win->closeWindow();
        break;
    case Options::MouseActivateRaiseAndMove:
    case Options::MouseActivateRaiseAndUnrestrictedMove:
        workspace()->raiseClient(win);
        workspace()->requestFocus(win);
        screens()->setCurrent(globalPos);
        // Fallthrough
    case Options::MouseMove:
    case Options::MouseUnrestrictedMove: {
        if (!win->isMovableAcrossScreens()) {
            break;
        }
        if (win->isMoveResize()) {
            finish_move_resize(win, false);
        }
        win->setMoveResizePointerMode(position::center);
        win->setMoveResizePointerButtonDown(true);

        // map from global
        win->setMoveOffset(QPoint(globalPos.x() - win->x(), globalPos.y() - win->y()));

        win->setInvertedMoveOffset(win->rect().bottomRight() - win->moveOffset());
        win->setUnrestrictedMoveResize((cmd == Options::MouseActivateRaiseAndUnrestrictedMove
                                        || cmd == Options::MouseUnrestrictedMove));
        if (!start_move_resize(win)) {
            win->setMoveResizePointerButtonDown(false);
        }
        win->updateCursor();
        break;
    }
    case Options::MouseResize:
    case Options::MouseUnrestrictedResize: {
        if (!win->isResizable() || win->isShade()) {
            break;
        }
        if (win->isMoveResize()) {
            finish_move_resize(win, false);
        }
        win->setMoveResizePointerButtonDown(true);

        // Map from global
        auto const moveOffset = QPoint(globalPos.x() - win->x(), globalPos.y() - win->y());
        win->setMoveOffset(moveOffset);

        auto x = moveOffset.x();
        auto y = moveOffset.y();
        auto left = x < win->width() / 3;
        auto right = x >= 2 * win->width() / 3;
        auto top = y < win->height() / 3;
        auto bot = y >= 2 * win->height() / 3;

        position mode;
        if (top) {
            mode = left ? position::top_left : (right ? position::top_right : position::top);
        } else if (bot) {
            mode = left ? position::bottom_left
                        : (right ? position::bottom_right : position::bottom);
        } else {
            mode = (x < win->width() / 2) ? position::left : position::right;
        }
        win->setMoveResizePointerMode(mode);
        win->setInvertedMoveOffset(win->rect().bottomRight() - moveOffset);
        win->setUnrestrictedMoveResize((cmd == Options::MouseUnrestrictedResize));
        if (!start_move_resize(win)) {
            win->setMoveResizePointerButtonDown(false);
        }
        win->updateCursor();
        break;
    }

    case Options::MouseNothing:
    default:
        replay = true;
        break;
    }
    return replay;
}

template<typename Win>
void enter_event(Win* win, const QPoint& globalPos)
{
    // TODO: shade hover
    if (options->focusPolicy() == Options::ClickToFocus
        || workspace()->userActionsMenu()->isShown()) {
        return;
    }

    if (options->isAutoRaise() && !win::is_desktop(win) && !win::is_dock(win)
        && workspace()->focusChangeEnabled() && globalPos != workspace()->focusMousePosition()
        && workspace()->topClientOnDesktop(VirtualDesktopManager::self()->current(),
                                           options->isSeparateScreenFocus() ? win->screen() : -1)
            != win) {
        win->control()->start_auto_raise();
    }

    if (win::is_desktop(win) || win::is_dock(win)) {
        return;
    }

    // For FocusFollowsMouse, change focus only if the mouse has actually been moved, not if the
    // focus change came because of window changes (e.g. closing a window) - #92290
    if (options->focusPolicy() != Options::FocusFollowsMouse
        || globalPos != workspace()->focusMousePosition()) {
        workspace()->requestDelayFocus(win);
    }
}

template<typename Win>
void leave_event(Win* win)
{
    win->control()->cancel_auto_raise();
    workspace()->cancelDelayFocus();
    // TODO: shade hover
    // TODO: send hover leave to deco
    // TODO: handle Options::FocusStrictlyUnderMouse
}

template<typename Win>
bool titlebar_positioned_under_mouse(Win* win)
{
    if (!win->isDecorated()) {
        return false;
    }

    auto const section = win->decoration()->sectionUnderMouse();
    if (section == Qt::TitleBarArea) {
        return true;
    }

    // Check other sections based on titlebarPosition.
    switch (win->titlebarPosition()) {
    case position::top:
        return (section == Qt::TopLeftSection || section == Qt::TopSection
                || section == Qt::TopRightSection);
    case position::left:
        return (section == Qt::TopLeftSection || section == Qt::LeftSection
                || section == Qt::BottomLeftSection);
    case position::right:
        return (section == Qt::BottomRightSection || section == Qt::RightSection
                || section == Qt::TopRightSection);
    case position::bottom:
        return (section == Qt::BottomLeftSection || section == Qt::BottomSection
                || section == Qt::BottomRightSection);
    default:
        // Nothing
        return false;
    }
}

template<typename Win>
void process_decoration_move(Win* win, QPoint const& localPos, QPoint const& globalPos)
{
    if (win->isMoveResizePointerButtonDown()) {
        move_resize(win, localPos.x(), localPos.y(), globalPos.x(), globalPos.y());
        return;
    }

    // TODO: handle modifiers
    auto newmode = mouse_position(win);
    if (newmode != win->moveResizePointerMode()) {
        win->setMoveResizePointerMode(newmode);
        win->updateCursor();
    }
}

template<typename Win>
void process_decoration_button_release(Win* win, QMouseEvent* event)
{
    if (win->isDecorated()) {
        if (event->isAccepted() || !titlebar_positioned_under_mouse(win)) {
            // Click was for the deco and shall not init a doubleclick.
            win->invalidateDecorationDoubleClickTimer();
        }
    }

    if (event->buttons() == Qt::NoButton) {
        win->setMoveResizePointerButtonDown(false);
        win->stopDelayedMoveResize();
        if (win->isMoveResize()) {
            finish_move_resize(win, false);
            win->setMoveResizePointerMode(mouse_position(win));
        }
        win->updateCursor();
    }
}

/**
 * Determines the mouse command for the given @p button in the current state.
 *
 * The @p handled argument specifies whether the button was handled or not.
 * This value should be used to determine whether the mouse button should be
 * passed to @p win or being filtered out.
 */
template<typename Win>
Options::MouseCommand get_mouse_command(Win* win, Qt::MouseButton button, bool* handled)
{
    *handled = false;
    if (button == Qt::NoButton) {
        return Options::MouseNothing;
    }
    if (win->control()->active()) {
        if (options->isClickRaise() && !is_most_recently_raised(win)) {
            *handled = true;
            return Options::MouseActivateRaiseAndPassClick;
        }
    } else {
        *handled = true;
        switch (button) {
        case Qt::LeftButton:
            return options->commandWindow1();
        case Qt::MiddleButton:
            return options->commandWindow2();
        case Qt::RightButton:
            return options->commandWindow3();
        default:
            // all other buttons pass Activate & Pass Client
            return Options::MouseActivateAndPassClick;
        }
    }
    return Options::MouseNothing;
}

template<typename Win>
Options::MouseCommand get_wheel_command(Win* win, Qt::Orientation orientation, bool* handled)
{
    *handled = false;
    if (orientation != Qt::Vertical) {
        return Options::MouseNothing;
    }
    if (!win->control()->active()) {
        *handled = true;
        return options->commandWindowWheel();
    }
    return Options::MouseNothing;
}

}

#endif
