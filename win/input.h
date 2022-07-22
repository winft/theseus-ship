/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "actions.h"
#include "control.h"
#include "move.h"
#include "net.h"
#include "screen.h"
#include "stacking_order.h"
#include "toplevel.h"
#include "types.h"
#include "user_actions_menu.h"
#include "window_operation.h"

#include "base/options.h"
#include "utils/blocker.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QMouseEvent>
#include <QStyleHints>

namespace KWin::win
{

template<typename Win>
bool is_most_recently_raised(Win* win)
{
    // The last toplevel in the unconstrained stacking order is the most recently raised one.
    auto last = top_client_on_desktop(
        &win->space, win->space.virtual_desktop_manager->current(), nullptr, true, false);
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
bool perform_mouse_command(Win& win, base::options::MouseCommand cmd, QPoint const& globalPos)
{
    bool replay = false;
    auto& base = kwinApp()->get_base();
    auto& space = win.space;

    switch (cmd) {
    case base::options::MouseRaise:
        raise_window(&space, &win);
        break;
    case base::options::MouseLower: {
        lower_window(&space, &win);
        // Used to be activateNextClient(win), then topClientOnDesktop
        // since win is a mouseOp it's however safe to use the client under the mouse instead.
        if (win.control->active() && kwinApp()->options->focusPolicyIsReasonable()) {
            auto next = window_under_mouse(space, win.central_output);
            if (next && next != &win) {
                request_focus(space, next);
            }
        }
        break;
    }
    case base::options::MouseOperationsMenu:
        if (win.control->active() && kwinApp()->options->isClickRaise()) {
            auto_raise(&win);
        }
        space.user_actions_menu->show(QRect(globalPos, globalPos), &win);
        break;
    case base::options::MouseToggleRaiseAndLower:
        raise_or_lower_client(&space, &win);
        break;
    case base::options::MouseActivateAndRaise: {
        // For clickraise mode.
        replay = win.control->active();
        bool mustReplay = !win.control->rules().checkAcceptFocus(win.acceptsFocus());

        if (mustReplay) {
            auto it = space.stacking_order->stack.cend();
            auto begin = space.stacking_order->stack.cbegin();
            while (mustReplay && --it != begin && *it != &win) {
                auto window = *it;
                if (!window->control
                    || (window->control->keep_above() && !win.control->keep_above())
                    || (win.control->keep_below() && !window->control->keep_below())) {
                    // Can never raise above "it".
                    continue;
                }
                mustReplay = !(window->isOnCurrentDesktop()
                               && window->frameGeometry().intersects(win.frameGeometry()));
            }
        }

        request_focus(space, &win, true);
        base::set_current_output_by_position(base, globalPos);
        replay = replay || mustReplay;
        break;
    }
    case base::options::MouseActivateAndLower:
        request_focus(space, &win);
        lower_window(&space, &win);
        base::set_current_output_by_position(base, globalPos);
        replay = replay || !win.control->rules().checkAcceptFocus(win.acceptsFocus());
        break;
    case base::options::MouseActivate:
        // For clickraise mode.
        replay = win.control->active();
        request_focus(space, &win);
        base::set_current_output_by_position(base, globalPos);
        replay = replay || !win.control->rules().checkAcceptFocus(win.acceptsFocus());
        break;
    case base::options::MouseActivateRaiseAndPassClick:
        request_focus(space, &win, true);
        base::set_current_output_by_position(base, globalPos);
        replay = true;
        break;
    case base::options::MouseActivateAndPassClick:
        request_focus(space, &win);
        base::set_current_output_by_position(base, globalPos);
        replay = true;
        break;
    case base::options::MouseMaximize:
        maximize(&win, maximize_mode::full);
        break;
    case base::options::MouseRestore:
        maximize(&win, maximize_mode::restore);
        break;
    case base::options::MouseMinimize:
        set_minimized(&win, true);
        break;
    case base::options::MouseAbove: {
        blocker block(space.stacking_order);
        if (win.control->keep_below()) {
            set_keep_below(&win, false);
        } else {
            set_keep_above(&win, true);
        }
        break;
    }
    case base::options::MouseBelow: {
        blocker block(space.stacking_order);
        if (win.control->keep_above()) {
            set_keep_above(&win, false);
        } else {
            set_keep_below(&win, true);
        }
        break;
    }
    case base::options::MousePreviousDesktop:
        window_to_prev_desktop(win);
        break;
    case base::options::MouseNextDesktop:
        window_to_next_desktop(win);
        break;
    case base::options::MouseOpacityMore:
        // No point in changing the opacity of the desktop.
        if (!is_desktop(&win)) {
            win.setOpacity(qMin(win.opacity() + 0.1, 1.0));
        }
        break;
    case base::options::MouseOpacityLess:
        if (!is_desktop(&win)) {
            win.setOpacity(qMax(win.opacity() - 0.1, 0.1));
        }
        break;
    case base::options::MouseClose:
        win.closeWindow();
        break;
    case base::options::MouseActivateRaiseAndMove:
    case base::options::MouseActivateRaiseAndUnrestrictedMove:
        raise_window(&space, &win);
        request_focus(space, &win);
        base::set_current_output_by_position(base, globalPos);
        // Fallthrough
    case base::options::MouseMove:
    case base::options::MouseUnrestrictedMove: {
        if (!win.isMovableAcrossScreens()) {
            break;
        }

        auto& mov_res = win.control->move_resize();
        if (mov_res.enabled) {
            finish_move_resize(&win, false);
        }
        mov_res.contact = position::center;
        mov_res.button_down = true;

        // map from global
        mov_res.offset = QPoint(globalPos.x() - win.pos().x(), globalPos.y() - win.pos().y());

        mov_res.inverted_offset
            = QPoint(win.size().width() - 1, win.size().height() - 1) - mov_res.offset;
        mov_res.unrestricted = (cmd == base::options::MouseActivateRaiseAndUnrestrictedMove
                                || cmd == base::options::MouseUnrestrictedMove);
        if (!start_move_resize(&win)) {
            mov_res.button_down = false;
        }
        update_cursor(&win);
        break;
    }
    case base::options::MouseResize:
    case base::options::MouseUnrestrictedResize: {
        if (!win.isResizable()) {
            break;
        }
        auto& mov_res = win.control->move_resize();
        if (mov_res.enabled) {
            finish_move_resize(&win, false);
        }
        mov_res.button_down = true;

        // Map from global
        auto const moveOffset
            = QPoint(globalPos.x() - win.pos().x(), globalPos.y() - win.pos().y());
        mov_res.offset = moveOffset;

        auto x = moveOffset.x();
        auto y = moveOffset.y();
        auto left = x < win.size().width() / 3;
        auto right = x >= 2 * win.size().width() / 3;
        auto top = y < win.size().height() / 3;
        auto bot = y >= 2 * win.size().height() / 3;

        position mode;
        if (top) {
            mode = left ? position::top_left : (right ? position::top_right : position::top);
        } else if (bot) {
            mode = left ? position::bottom_left
                        : (right ? position::bottom_right : position::bottom);
        } else {
            mode = (x < win.size().width() / 2) ? position::left : position::right;
        }
        mov_res.contact = mode;
        mov_res.inverted_offset
            = QPoint(win.size().width() - 1, win.size().height() - 1) - moveOffset;
        mov_res.unrestricted = cmd == base::options::MouseUnrestrictedResize;
        if (!start_move_resize(&win)) {
            mov_res.button_down = false;
        }
        update_cursor(&win);
        break;
    }

    case base::options::MouseNothing:
    default:
        replay = true;
        break;
    }
    return replay;
}

template<typename Win>
void enter_event(Win* win, const QPoint& globalPos)
{
    auto space = &win->space;

    if (kwinApp()->options->focusPolicy() == base::options::ClickToFocus
        || space->user_actions_menu->isShown()) {
        return;
    }

    if (kwinApp()->options->isAutoRaise() && !win::is_desktop(win) && !win::is_dock(win)
        && is_focus_change_allowed(*space) && globalPos != space->focusMousePos
        && top_client_on_desktop(space,
                                 win->space.virtual_desktop_manager->current(),
                                 kwinApp()->options->isSeparateScreenFocus() ? win->central_output
                                                                             : nullptr)
            != win) {
        win->control->start_auto_raise();
    }

    if (win::is_desktop(win) || win::is_dock(win)) {
        return;
    }

    // For FocusFollowsMouse, change focus only if the mouse has actually been moved, not if the
    // focus change came because of window changes (e.g. closing a window) - #92290
    if (kwinApp()->options->focusPolicy() != base::options::FocusFollowsMouse
        || globalPos != space->focusMousePos) {
        request_delay_focus(*space, win);
    }
}

template<typename Win>
void leave_event(Win* win)
{
    win->control->cancel_auto_raise();
    cancel_delay_focus(win->space);
    // TODO: send hover leave to deco
    // TODO: handle base::options::FocusStrictlyUnderMouse
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
    auto com = base::options::MouseNothing;
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
                perform_window_operation(
                    win->space, win, kwinApp()->options->operationTitlebarDblClick());
                end_move_resize(win);
                return false;
            }
        } else {
            // New first click and potential init, could be invalidated by release - see below.
            deco.double_click.start();
        }
    }

    if (event->button() == Qt::LeftButton) {
        com = active ? kwinApp()->options->commandActiveTitlebar1()
                     : kwinApp()->options->commandInactiveTitlebar1();
    } else if (event->button() == Qt::MiddleButton) {
        com = active ? kwinApp()->options->commandActiveTitlebar2()
                     : kwinApp()->options->commandInactiveTitlebar2();
    } else if (event->button() == Qt::RightButton) {
        com = active ? kwinApp()->options->commandActiveTitlebar3()
                     : kwinApp()->options->commandInactiveTitlebar3();
    }

    // Operations menu is for actions where it's not possible to get the matching and
    // mouse minimize for mouse release event.
    if (event->button() == Qt::LeftButton && com != base::options::MouseOperationsMenu
        && com != base::options::MouseMinimize) {
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
    if (!ignoreMenu || com != base::options::MouseOperationsMenu) {
        win->performMouseCommand(com, event->globalPos());
    }

    // Return events that should be passed to the decoration in the new API.
    return !(com == base::options::MouseRaise || com == base::options::MouseOperationsMenu
             || com == base::options::MouseActivateAndRaise || com == base::options::MouseActivate
             || com == base::options::MouseActivateRaiseAndPassClick
             || com == base::options::MouseActivateAndPassClick
             || com == base::options::MouseNothing);
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
base::options::MouseCommand get_mouse_command(Win* win, Qt::MouseButton button, bool* handled)
{
    *handled = false;
    if (button == Qt::NoButton) {
        return base::options::MouseNothing;
    }
    if (win->control->active()) {
        if (kwinApp()->options->isClickRaise() && !is_most_recently_raised(win)) {
            *handled = true;
            return base::options::MouseActivateRaiseAndPassClick;
        }
    } else {
        *handled = true;
        switch (button) {
        case Qt::LeftButton:
            return kwinApp()->options->commandWindow1();
        case Qt::MiddleButton:
            return kwinApp()->options->commandWindow2();
        case Qt::RightButton:
            return kwinApp()->options->commandWindow3();
        default:
            // all other buttons pass Activate & Pass Client
            return base::options::MouseActivateAndPassClick;
        }
    }
    return base::options::MouseNothing;
}

template<typename Win>
base::options::MouseCommand get_wheel_command(Win* win, Qt::Orientation orientation, bool* handled)
{
    *handled = false;
    if (orientation != Qt::Vertical) {
        return base::options::MouseNothing;
    }
    if (!win->control->active()) {
        *handled = true;
        return kwinApp()->options->commandWindowWheel();
    }
    return base::options::MouseNothing;
}

template<typename Space>
void set_global_shortcuts_disabled(Space& space, bool disable)
{
    if (space.global_shortcuts_disabled == disable) {
        return;
    }

    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kglobalaccel"),
                                                          QStringLiteral("/kglobalaccel"),
                                                          QStringLiteral("org.kde.KGlobalAccel"),
                                                          QStringLiteral("blockGlobalShortcuts"));
    message.setArguments(QList<QVariant>() << disable);
    QDBusConnection::sessionBus().asyncCall(message);

    space.global_shortcuts_disabled = disable;

    // Update also Meta+LMB actions etc.
    for (auto window : space.windows) {
        if (auto& ctrl = window->control) {
            ctrl->update_mouse_grab();
        }
    }
}

}
