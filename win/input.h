/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "control.h"
#include "layers.h"
#include "move.h"
#include "net.h"
#include "options.h"
#include "space.h"
#include "stacking.h"
#include "stacking_order.h"
#include "toplevel.h"
#include "types.h"
#include "user_actions_menu.h"

#include "utils/blocker.h"

#include <QMouseEvent>
#include <QStyleHints>

namespace KWin::win
{

template<typename Win>
bool is_most_recently_raised(Win* win)
{
    // The last toplevel in the unconstrained stacking order is the most recently raised one.
    auto last = top_client_on_desktop(
        workspace(), virtual_desktop_manager::self()->current(), -1, true, false);
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
    auto pos = input::get_cursor()->pos();

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
        win->control->move_resize().button_down = false;
        finish_move_resize(win, false);
        update_cursor(win);
        break;
    case Qt::Key_Escape:
        win->control->move_resize().button_down = false;
        finish_move_resize(win, true);
        update_cursor(win);
        break;
    default:
        return;
    }
    input::get_cursor()->set_pos(pos);
}

template<typename Win>
bool perform_mouse_command(Win* win, Options::MouseCommand cmd, QPoint const& globalPos)
{
    bool replay = false;
    auto& screens = kwinApp()->get_base().screens;

    switch (cmd) {
    case Options::MouseRaise:
        raise_window(workspace(), win);
        break;
    case Options::MouseLower: {
        lower_window(workspace(), win);
        // Used to be activateNextClient(win), then topClientOnDesktop
        // since win is a mouseOp it's however safe to use the client under the mouse instead.
        if (win->control->active() && options->focusPolicyIsReasonable()) {
            auto next = workspace()->clientUnderMouse(win->screen());
            if (next && next != win)
                workspace()->request_focus(next);
        }
        break;
    }
    case Options::MouseOperationsMenu:
        if (win->control->active() && options->isClickRaise()) {
            auto_raise(win);
        }
        workspace()->showWindowMenu(QRect(globalPos, globalPos), win);
        break;
    case Options::MouseToggleRaiseAndLower:
        raise_or_lower_client(workspace(), win);
        break;
    case Options::MouseActivateAndRaise: {
        // For clickraise mode.
        replay = win->control->active();
        bool mustReplay = !win->control->rules().checkAcceptFocus(win->acceptsFocus());

        if (mustReplay) {
            auto it = workspace()->stacking_order->sorted().cend();
            auto begin = workspace()->stacking_order->sorted().cbegin();
            while (mustReplay && --it != begin && *it != win) {
                auto window = *it;
                if (!window->control
                    || (window->control->keep_above() && !win->control->keep_above())
                    || (win->control->keep_below() && !window->control->keep_below())) {
                    // Can never raise above "it".
                    continue;
                }
                mustReplay = !(window->isOnCurrentDesktop()
                               && window->frameGeometry().intersects(win->frameGeometry()));
            }
        }

        workspace()->request_focus(win, true);
        screens.setCurrent(globalPos);
        replay = replay || mustReplay;
        break;
    }
    case Options::MouseActivateAndLower:
        workspace()->request_focus(win);
        lower_window(workspace(), win);
        screens.setCurrent(globalPos);
        replay = replay || !win->control->rules().checkAcceptFocus(win->acceptsFocus());
        break;
    case Options::MouseActivate:
        // For clickraise mode.
        replay = win->control->active();
        workspace()->request_focus(win);
        screens.setCurrent(globalPos);
        replay = replay || !win->control->rules().checkAcceptFocus(win->acceptsFocus());
        break;
    case Options::MouseActivateRaiseAndPassClick:
        workspace()->request_focus(win, true);
        screens.setCurrent(globalPos);
        replay = true;
        break;
    case Options::MouseActivateAndPassClick:
        workspace()->request_focus(win);
        screens.setCurrent(globalPos);
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
        blocker block(workspace()->stacking_order);
        if (win->control->keep_below()) {
            set_keep_below(win, false);
        } else {
            set_keep_above(win, true);
        }
        break;
    }
    case Options::MouseBelow: {
        blocker block(workspace()->stacking_order);
        if (win->control->keep_above()) {
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
        raise_window(workspace(), win);
        workspace()->request_focus(win);
        screens.setCurrent(globalPos);
        // Fallthrough
    case Options::MouseMove:
    case Options::MouseUnrestrictedMove: {
        if (!win->isMovableAcrossScreens()) {
            break;
        }

        auto& mov_res = win->control->move_resize();
        if (mov_res.enabled) {
            finish_move_resize(win, false);
        }
        mov_res.contact = position::center;
        mov_res.button_down = true;

        // map from global
        mov_res.offset = QPoint(globalPos.x() - win->pos().x(), globalPos.y() - win->pos().y());

        mov_res.inverted_offset
            = QPoint(win->size().width() - 1, win->size().height() - 1) - mov_res.offset;
        mov_res.unrestricted = (cmd == Options::MouseActivateRaiseAndUnrestrictedMove
                                || cmd == Options::MouseUnrestrictedMove);
        if (!start_move_resize(win)) {
            mov_res.button_down = false;
        }
        update_cursor(win);
        break;
    }
    case Options::MouseResize:
    case Options::MouseUnrestrictedResize: {
        if (!win->isResizable()) {
            break;
        }
        auto& mov_res = win->control->move_resize();
        if (mov_res.enabled) {
            finish_move_resize(win, false);
        }
        mov_res.button_down = true;

        // Map from global
        auto const moveOffset
            = QPoint(globalPos.x() - win->pos().x(), globalPos.y() - win->pos().y());
        mov_res.offset = moveOffset;

        auto x = moveOffset.x();
        auto y = moveOffset.y();
        auto left = x < win->size().width() / 3;
        auto right = x >= 2 * win->size().width() / 3;
        auto top = y < win->size().height() / 3;
        auto bot = y >= 2 * win->size().height() / 3;

        position mode;
        if (top) {
            mode = left ? position::top_left : (right ? position::top_right : position::top);
        } else if (bot) {
            mode = left ? position::bottom_left
                        : (right ? position::bottom_right : position::bottom);
        } else {
            mode = (x < win->size().width() / 2) ? position::left : position::right;
        }
        mov_res.contact = mode;
        mov_res.inverted_offset
            = QPoint(win->size().width() - 1, win->size().height() - 1) - moveOffset;
        mov_res.unrestricted = cmd == Options::MouseUnrestrictedResize;
        if (!start_move_resize(win)) {
            mov_res.button_down = false;
        }
        update_cursor(win);
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
    if (options->focusPolicy() == Options::ClickToFocus
        || workspace()->userActionsMenu()->isShown()) {
        return;
    }

    if (options->isAutoRaise() && !win::is_desktop(win) && !win::is_dock(win)
        && workspace()->focusChangeEnabled() && globalPos != workspace()->focusMousePosition()
        && top_client_on_desktop(workspace(),
                                 virtual_desktop_manager::self()->current(),
                                 options->isSeparateScreenFocus() ? win->screen() : -1)
            != win) {
        win->control->start_auto_raise();
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
    win->control->cancel_auto_raise();
    workspace()->cancelDelayFocus();
    // TODO: send hover leave to deco
    // TODO: handle Options::FocusStrictlyUnderMouse
}

template<typename Win>
bool titlebar_positioned_under_mouse(Win* win)
{
    if (!decoration(win)) {
        return false;
    }

    auto const section = decoration(win)->sectionUnderMouse();
    return section == Qt::TitleBarArea || section == Qt::TopLeftSection || section == Qt::TopSection
        || section == Qt::TopRightSection;
}

template<typename Win>
bool process_decoration_button_press(Win* win, QMouseEvent* event, bool ignoreMenu)
{
    auto com = Options::MouseNothing;
    bool active = win->control->active();

    if (!win->wantsInput()) {
        // We cannot be active, use it anyway.
        active = true;
    }

    // check whether it is a double click
    if (event->button() == Qt::LeftButton && titlebar_positioned_under_mouse(win)) {
        auto& deco = win->control->deco();
        if (deco.double_click.active()) {
            auto const interval = deco.double_click.stop();
            if (interval > QGuiApplication::styleHints()->mouseDoubleClickInterval()) {
                // expired -> new first click and pot. init
                deco.double_click.start();
            } else {
                workspace()->performWindowOperation(win, options->operationTitlebarDblClick());
                end_move_resize(win);
                return false;
            }
        } else {
            // New first click and potential init, could be invalidated by release - see below.
            deco.double_click.start();
        }
    }

    if (event->button() == Qt::LeftButton) {
        com = active ? options->commandActiveTitlebar1() : options->commandInactiveTitlebar1();
    } else if (event->button() == Qt::MiddleButton) {
        com = active ? options->commandActiveTitlebar2() : options->commandInactiveTitlebar2();
    } else if (event->button() == Qt::RightButton) {
        com = active ? options->commandActiveTitlebar3() : options->commandInactiveTitlebar3();
    }

    // Operations menu is for actions where it's not possible to get the matching and
    // mouse minimize for mouse release event.
    if (event->button() == Qt::LeftButton && com != Options::MouseOperationsMenu
        && com != Options::MouseMinimize) {
        auto& mov_res = win->control->move_resize();

        mov_res.contact = win::mouse_position(win);
        mov_res.button_down = true;
        mov_res.offset = event->pos();

        // TODO: use win's size instead.
        mov_res.inverted_offset
            = QPoint(win->size().width() - 1, win->size().height() - 1) - mov_res.offset;
        mov_res.unrestricted = false;
        win::start_delayed_move_resize(win);
        win::update_cursor(win);
    }
    // In the new API the decoration may process the menu action to display an inactive tab's menu.
    // If the event is unhandled then the core will create one for the active window in the group.
    if (!ignoreMenu || com != Options::MouseOperationsMenu) {
        win->performMouseCommand(com, event->globalPos());
    }

    // Return events that should be passed to the decoration in the new API.
    return !(com == Options::MouseRaise || com == Options::MouseOperationsMenu
             || com == Options::MouseActivateAndRaise || com == Options::MouseActivate
             || com == Options::MouseActivateRaiseAndPassClick
             || com == Options::MouseActivateAndPassClick || com == Options::MouseNothing);
}

template<typename Win>
void process_decoration_move(Win* win, QPoint const& localPos, QPoint const& globalPos)
{
    auto& mov_res = win->control->move_resize();
    if (mov_res.button_down) {
        // TODO(romangg): Can we simply call move_resize here?
        move_resize_impl(win, localPos.x(), localPos.y(), globalPos.x(), globalPos.y());
        return;
    }

    // TODO: handle modifiers
    auto newmode = mouse_position(win);
    if (newmode != mov_res.contact) {
        mov_res.contact = newmode;
        update_cursor(win);
    }
}

template<typename Win>
void process_decoration_button_release(Win* win, QMouseEvent* event)
{
    if (decoration(win)) {
        if (event->isAccepted() || !titlebar_positioned_under_mouse(win)) {
            // Click was for the deco and shall not init a doubleclick.
            win->control->deco().double_click.stop();
        }
    }

    if (event->buttons() == Qt::NoButton) {
        auto& mov_res = win->control->move_resize();
        mov_res.button_down = false;
        stop_delayed_move_resize(win);
        if (mov_res.enabled) {
            finish_move_resize(win, false);
            mov_res.contact = mouse_position(win);
        }
        update_cursor(win);
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
    if (win->control->active()) {
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
    if (!win->control->active()) {
        *handled = true;
        return options->commandWindowWheel();
    }
    return Options::MouseNothing;
}

template<typename Win>
void set_shortcut(Win* win, QString const& shortcut)
{
    auto update_shortcut = [&win](QKeySequence const& cut = QKeySequence()) {
        if (win->control->shortcut() == cut) {
            return;
        }
        win->control->set_shortcut(cut.toString());
        win->setShortcutInternal();
    };

    auto cut = win->control->rules().checkShortcut(shortcut);
    if (cut.isEmpty()) {
        update_shortcut();
        return;
    }
    if (cut == win->control->shortcut().toString()) {
        // No change
        return;
    }

    // Format:
    //       base+(abcdef)<space>base+(abcdef)
    //   Alt+Ctrl+(ABCDEF);Meta+X,Meta+(ABCDEF)
    //
    if (!cut.contains(QLatin1Char('(')) && !cut.contains(QLatin1Char(')'))
        && !cut.contains(QLatin1String(" - "))) {
        if (workspace()->shortcutAvailable(cut, win)) {
            update_shortcut(QKeySequence(cut));
        } else {
            update_shortcut();
        }
        return;
    }

    QRegularExpression const reg(QStringLiteral("(.*\\+)\\((.*)\\)"));
    QList<QKeySequence> keys;
    QStringList groups = cut.split(QStringLiteral(" - "));
    for (QStringList::ConstIterator it = groups.constBegin(); it != groups.constEnd(); ++it) {
        auto const match = reg.match(*it);
        if (match.hasMatch()) {
            auto const base = match.captured(1);
            auto const list = match.captured(2);

            for (int i = 0; i < list.length(); ++i) {
                QKeySequence c(base + list[i]);
                if (!c.isEmpty()) {
                    keys.append(c);
                }
            }
        } else {
            // The regexp doesn't match, so it should be a normal shortcut.
            QKeySequence c(*it);
            if (!c.isEmpty()) {
                keys.append(c);
            }
        }
    }

    for (auto it = keys.constBegin(); it != keys.constEnd(); ++it) {
        if (win->control->shortcut() == *it) {
            // Current one is in the list.
            return;
        }
    }
    for (auto it = keys.constBegin(); it != keys.constEnd(); ++it) {
        if (workspace()->shortcutAvailable(*it, win)) {
            update_shortcut(*it);
            return;
        }
    }
    update_shortcut();
}

}
