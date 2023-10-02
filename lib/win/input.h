/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "actions.h"
#include "desktop_space.h"
#include "move.h"
#include "net.h"
#include "screen.h"
#include "stacking_order.h"
#include "types.h"
#include <win/activation.h>

#include "base/options.h"
#include "utils/blocker.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QStyleHints>

namespace KWin::win
{

/// Maps from global to window coordinates.
template<typename Win>
QMatrix4x4 get_input_transform(Win& win)
{
    QMatrix4x4 transform;

    auto const render_pos = frame_to_render_pos(&win, win.geo.pos());
    transform.translate(-render_pos.x(), -render_pos.y());

    return transform;
}

template<typename Win>
bool is_most_recently_raised(Win* win)
{
    using var_win = typename Win::space_t::window_t;

    // The last toplevel in the unconstrained stacking order is the most recently raised one.
    auto last = top_client_on_desktop(
        win->space, win->space.virtual_desktop_manager->current(), nullptr, true, false);
    return last == var_win(win);
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
    auto pos = win->space.input->cursor->pos();

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
        win->control->move_resize.button_down = false;
        finish_move_resize(win, false);
        update_cursor(win);
        break;
    case Qt::Key_Escape:
        win->control->move_resize.button_down = false;
        finish_move_resize(win, true);
        update_cursor(win);
        break;
    default:
        return;
    }
    win->space.input->cursor->set_pos(pos);
}

template<typename Win>
bool perform_mouse_command(Win& win, mouse_cmd cmd, QPoint const& globalPos)
{
    using var_win = typename Win::space_t::window_t;
    bool replay = false;
    auto& space = win.space;
    auto& base = space.base;

    switch (cmd) {
    case mouse_cmd::raise:
        raise_window(space, &win);
        break;
    case mouse_cmd::lower: {
        lower_window(space, &win);
        // Used to be activateNextClient(win), then topClientOnDesktop
        // since win is a mouseOp it's however safe to use the client under the mouse
        // instead.
        if (win.control->active && win.space.options->qobject->focusPolicyIsReasonable()) {
            auto next = window_under_mouse(space, win.topo.central_output);
            if (next && *next != var_win(&win)) {
                std::visit(overload{[&](auto&& next) { request_focus(space, *next); }}, *next);
            }
        }
        break;
    }
    case mouse_cmd::operations_menu:
        if (win.control->active && win.space.options->qobject->isClickRaise()) {
            auto_raise(win);
        }
        space.user_actions_menu->show(QRect(globalPos, globalPos), &win);
        break;
    case mouse_cmd::toggle_raise_and_lower:
        raise_or_lower_client(space, &win);
        break;
    case mouse_cmd::activate_and_raise: {
        // For clickraise mode.
        replay = win.control->active;
        bool mustReplay = !win.control->rules.checkAcceptFocus(win.acceptsFocus());

        if (mustReplay) {
            auto it = space.stacking.order.stack.cend();
            auto begin = space.stacking.order.stack.cbegin();
            while (mustReplay && --it != begin && *it != var_win(&win)) {
                std::visit(overload{[&](auto&& cmp_win) {
                               if (!cmp_win->control
                                   || (cmp_win->control->keep_above && !win.control->keep_above)
                                   || (win.control->keep_below && !cmp_win->control->keep_below)) {
                                   // Can never raise above "it".
                                   return;
                               }
                               mustReplay = !(on_current_desktop(*cmp_win)
                                              && cmp_win->geo.frame.intersects(win.geo.frame));
                           }},
                           *it);
            }
        }

        request_focus(space, win, true);
        base::set_current_output_by_position(base, globalPos);
        replay = replay || mustReplay;
        break;
    }
    case mouse_cmd::activate_and_lower:
        request_focus(space, win);
        lower_window(space, &win);
        base::set_current_output_by_position(base, globalPos);
        replay = replay || !win.control->rules.checkAcceptFocus(win.acceptsFocus());
        break;
    case mouse_cmd::activate:
        // For clickraise mode.
        replay = win.control->active;
        request_focus(space, win);
        base::set_current_output_by_position(base, globalPos);
        replay = replay || !win.control->rules.checkAcceptFocus(win.acceptsFocus());
        break;
    case mouse_cmd::activate_raise_and_pass_click:
        request_focus(space, win, true);
        base::set_current_output_by_position(base, globalPos);
        replay = true;
        break;
    case mouse_cmd::activate_and_pass_click:
        request_focus(space, win);
        base::set_current_output_by_position(base, globalPos);
        replay = true;
        break;
    case mouse_cmd::maximize:
        maximize(&win, maximize_mode::full);
        break;
    case mouse_cmd::restore:
        maximize(&win, maximize_mode::restore);
        break;
    case mouse_cmd::minimize:
        set_minimized(&win, true);
        break;
    case mouse_cmd::above: {
        blocker block(space.stacking.order);
        if (win.control->keep_below) {
            set_keep_below(&win, false);
        } else {
            set_keep_above(&win, true);
        }
        break;
    }
    case mouse_cmd::below: {
        blocker block(space.stacking.order);
        if (win.control->keep_above) {
            set_keep_above(&win, false);
        } else {
            set_keep_below(&win, true);
        }
        break;
    }
    case mouse_cmd::previous_desktop:
        window_to_prev_desktop(win);
        break;
    case mouse_cmd::next_desktop:
        window_to_next_desktop(win);
        break;
    case mouse_cmd::opacity_more:
        // No point in changing the opacity of the desktop.
        if (!is_desktop(&win)) {
            win.setOpacity(qMin(win.opacity() + 0.1, 1.0));
        }
        break;
    case mouse_cmd::opacity_less:
        if (!is_desktop(&win)) {
            win.setOpacity(qMax(win.opacity() - 0.1, 0.1));
        }
        break;
    case mouse_cmd::close:
        win.closeWindow();
        break;
    case mouse_cmd::activate_raise_and_move:
    case mouse_cmd::activate_raise_and_unrestricted_move:
        raise_window(space, &win);
        request_focus(space, win);
        base::set_current_output_by_position(base, globalPos);
        // Fallthrough
    case mouse_cmd::move:
    case mouse_cmd::unrestricted_move: {
        if (!win.isMovableAcrossScreens()) {
            break;
        }

        auto& mov_res = win.control->move_resize;
        if (mov_res.enabled) {
            finish_move_resize(&win, false);
        }
        mov_res.contact = position::center;
        mov_res.button_down = true;

        // map from global
        mov_res.offset
            = QPoint(globalPos.x() - win.geo.pos().x(), globalPos.y() - win.geo.pos().y());

        mov_res.inverted_offset
            = QPoint(win.geo.size().width() - 1, win.geo.size().height() - 1) - mov_res.offset;
        mov_res.unrestricted = (cmd == mouse_cmd::activate_raise_and_unrestricted_move
                                || cmd == mouse_cmd::unrestricted_move);
        if (!start_move_resize(&win)) {
            mov_res.button_down = false;
        }
        update_cursor(&win);
        break;
    }
    case mouse_cmd::resize:
    case mouse_cmd::unrestricted_resize: {
        if (!win.isResizable()) {
            break;
        }
        auto& mov_res = win.control->move_resize;
        if (mov_res.enabled) {
            finish_move_resize(&win, false);
        }
        mov_res.button_down = true;

        // Map from global
        auto const moveOffset
            = QPoint(globalPos.x() - win.geo.pos().x(), globalPos.y() - win.geo.pos().y());
        mov_res.offset = moveOffset;

        auto x = moveOffset.x();
        auto y = moveOffset.y();
        auto left = x < win.geo.size().width() / 3;
        auto right = x >= 2 * win.geo.size().width() / 3;
        auto top = y < win.geo.size().height() / 3;
        auto bot = y >= 2 * win.geo.size().height() / 3;

        position mode;
        if (top) {
            mode = left ? position::top_left : (right ? position::top_right : position::top);
        } else if (bot) {
            mode = left ? position::bottom_left
                        : (right ? position::bottom_right : position::bottom);
        } else {
            mode = (x < win.geo.size().width() / 2) ? position::left : position::right;
        }
        mov_res.contact = mode;
        mov_res.inverted_offset
            = QPoint(win.geo.size().width() - 1, win.geo.size().height() - 1) - moveOffset;
        mov_res.unrestricted = cmd == mouse_cmd::unrestricted_resize;
        if (!start_move_resize(&win)) {
            mov_res.button_down = false;
        }
        update_cursor(&win);
        break;
    }

    case mouse_cmd::nothing:
    default:
        replay = true;
        break;
    }
    return replay;
}

template<typename Win>
void enter_event(Win* win, const QPoint& globalPos)
{
    using var_win = typename Win::space_t::window_t;
    auto& space = win->space;

    if (win->space.options->qobject->focusPolicy() == focus_policy::click
        || space.user_actions_menu->isShown()) {
        return;
    }

    if (win->space.options->qobject->isAutoRaise() && !win::is_desktop(win) && !win::is_dock(win)
        && is_focus_change_allowed(space) && globalPos != space.focusMousePos) {
        auto top = top_client_on_desktop(space,
                                         space.virtual_desktop_manager->current(),
                                         win->space.options->qobject->isSeparateScreenFocus()
                                             ? win->topo.central_output
                                             : nullptr);
        if (top != var_win(win)) {
            win->control->start_auto_raise();
        }
    }

    if (win::is_desktop(win) || win::is_dock(win)) {
        return;
    }

    // For FocusFollowsMouse, change focus only if the mouse has actually been moved, not if the
    // focus change came because of window changes (e.g. closing a window) - #92290
    if (win->space.options->qobject->focusPolicy() != focus_policy::follows_mouse
        || globalPos != space.focusMousePos) {
        space.stacking.delayfocus_window = win;
        reset_delay_focus_timer(space);
    }
}

template<typename Win>
void leave_event(Win* win)
{
    win->control->cancel_auto_raise();
    cancel_delay_focus(win->space);
    // TODO: send hover leave to deco
    // TODO: handle base::options_qobject::FocusStrictlyUnderMouse
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
void process_decoration_move(Win* win, QPoint const& localPos, QPoint const& globalPos)
{
    auto& mov_res = win->control->move_resize;
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
            win->control->deco.double_click.stop();
        }
    }

    if (event->buttons() == Qt::NoButton) {
        auto& mov_res = win->control->move_resize;
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
mouse_cmd get_mouse_command(Win* win, Qt::MouseButton button, bool* handled)
{
    *handled = false;
    if (button == Qt::NoButton) {
        return mouse_cmd::nothing;
    }
    if (win->control->active) {
        if (win->space.options->qobject->isClickRaise() && !is_most_recently_raised(win)) {
            *handled = true;
            return mouse_cmd::activate_raise_and_pass_click;
        }
    } else {
        *handled = true;
        switch (button) {
        case Qt::LeftButton:
            return win->space.options->qobject->commandWindow1();
        case Qt::MiddleButton:
            return win->space.options->qobject->commandWindow2();
        case Qt::RightButton:
            return win->space.options->qobject->commandWindow3();
        default:
            // all other buttons pass Activate & Pass Client
            return mouse_cmd::activate_and_pass_click;
        }
    }
    return mouse_cmd::nothing;
}

template<typename Win>
mouse_cmd get_wheel_command(Win* win, Qt::Orientation orientation, bool* handled)
{
    *handled = false;
    if (orientation != Qt::Vertical) {
        return mouse_cmd::nothing;
    }
    if (!win->control->active) {
        *handled = true;
        return win->space.options->qobject->commandWindowWheel();
    }
    return mouse_cmd::nothing;
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
    for (auto&& window : space.windows) {
        std::visit(overload{[](auto&& window) {
                       if (auto& ctrl = window->control) {
                           ctrl->update_mouse_grab();
                       }
                   }},
                   window);
    }
}

}
