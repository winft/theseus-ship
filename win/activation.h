/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "actions.h"
#include "desktop_set.h"
#include "focus_blocker.h"
#include "focus_chain_find.h"
#include "input.h"
#include "layers.h"
#include "screen.h"
#include "stacking.h"
#include "stacking_order.h"
#include "transient.h"
#include "window_find.h"
#include "x11/netinfo_helpers.h"
#include "x11/tool_windows.h"
#include "x11/user_time.h"

#include "utils/blocker.h"

namespace KWin::win
{

/**
 Prevention of focus stealing:

 KWin tries to prevent unwanted changes of focus, that would result
 from mapping a new window. Also, some nasty applications may try
 to force focus change even in cases when ICCCM 4.2.7 doesn't allow it
 (e.g. they may try to activate their main window because the user
 definitely "needs" to see something happened - misusing
 of QWidget::setActiveWindow() may be such case).

 There are 4 ways how a window may become active:
 - the user changes the active window (e.g. focus follows mouse, clicking
   on some window's titlebar) - the change of focus will
   be done by KWin, so there's nothing to solve in this case
 - the change of active window will be requested using the _NET_ACTIVE_WINDOW
   message (handled in RootInfo::changeActiveWindow()) - such requests
   will be obeyed, because this request is meant mainly for e.g. taskbar
   asking the WM to change the active window as a result of some user action.
   Normal applications should use this request only rarely in special cases.
   See also below the discussion of _NET_ACTIVE_WINDOW_TRANSFER.
 - the change of active window will be done by performing XSetInputFocus()
   on a window that's not currently active. ICCCM 4.2.7 describes when
   the application may perform change of input focus. In order to handle
   misbehaving applications, KWin will try to detect focus changes to
   windows that don't belong to currently active application, and restore
   focus back to the currently active window, instead of activating the window
   that got focus (unfortunately there's no way to FocusChangeRedirect similar
   to e.g. SubstructureRedirect, so there will be short time when the focus
   will be changed). The check itself that's done is
   space::allowClientActivation() (see below).
 - a new window will be mapped - this is the most complicated case. If
   the new window belongs to the currently active application, it may be safely
   mapped on top and activated. The same if there's no active window,
   or the active window is the desktop. These checks are done by
   space::allowClientActivation().
    Following checks need to compare times. One time is the timestamp
   of last user action in the currently active window, the other time is
   the timestamp of the action that originally caused mapping of the new window
   (e.g. when the application was started). If the first time is newer than
   the second one, the window will not be activated, as that indicates
   futher user actions took place after the action leading to this new
   mapped window. This check is done by space::allowClientActivation().
    There are several ways how to get the timestamp of action that caused
   the new mapped window (done in x11::window::readUserTimeMapTimestamp()) :
     - the window may have the _NET_WM_USER_TIME property. This way
       the application may either explicitly request that the window is not
       activated (by using 0 timestamp), or the property contains the time
       of last user action in the application.
     - KWin itself tries to detect time of last user action in every window,
       by watching KeyPress and ButtonPress events on windows. This way some
       events may be missed (if they don't propagate to the toplevel window),
       but it's good as a fallback for applications that don't provide
       _NET_WM_USER_TIME, and missing some events may at most lead
       to unwanted focus stealing.
     - the timestamp may come from application startup notification.
       Application startup notification, if it exists for the new mapped window,
       should include time of the user action that caused it.
     - if there's no timestamp available, it's checked whether the new window
       belongs to some already running application - if yes, the timestamp
       will be 0 (i.e. refuse activation)
     - if the window is from session restored window, the timestamp will
       be 0 too, unless this application was the active one at the time
       when the session was saved, in which case the window will be
       activated if there wasn't any user interaction since the time
       KWin was started.
     - as the last resort, the _KDE_NET_USER_CREATION_TIME timestamp
       is used. For every toplevel window that is created (see CreateNotify
       handling), this property is set to the at that time current time.
       Since at this time it's known that the new window doesn't belong
       to any existing application (better said, the application doesn't
       have any other window mapped), it is either the very first window
       of the application, or it is the only window of the application
       that was hidden before. The latter case is handled by removing
       the property from windows before withdrawing them, making
       the timestamp empty for next mapping of the window. In the sooner
       case, the timestamp will be used. This helps in case when
       an application is launched without application startup notification,
       it creates its mainwindow, and starts its initialization (that
       may possibly take long time). The timestamp used will be older
       than any user action done after launching this application.
     - if no timestamp is found at all, the window is activated.
    The check whether two windows belong to the same application (same
   process) is done in x11::window::belongToSameApplication(). Not 100% reliable,
   but hopefully 99,99% reliable.

 As a somewhat special case, window activation is always enabled when
 session saving is in progress. When session saving, the session
 manager allows only one application to interact with the user.
 Not allowing window activation in such case would result in e.g. dialogs
 not becoming active, so focus stealing prevention would cause here
 more harm than good.

 Windows that attempted to become active but KWin prevented this will
 be marked as demanding user attention. They'll get
 the _NET_WM_STATE_DEMANDS_ATTENTION state, and the taskbar should mark
 them specially (blink, etc.). The state will be reset when the window
 eventually really becomes active.

 There are two more ways how a window can become obtrusive, window stealing
 focus: By showing above the active window, by either raising itself,
 or by moving itself on the active desktop.
     - KWin will refuse raising non-active window above the active one,
         unless they belong to the same application. Applications shouldn't
         raise their windows anyway (unless the app wants to raise one
         of its windows above another of its windows).
     - KWin activates windows moved to the current desktop (as that seems
         logical from the user's point of view, after sending the window
         there directly from KWin, or e.g. using pager). This means
         applications shouldn't send their windows to another desktop
         (SELI TODO - but what if they do?)

 Special cases I can think of:
    - konqueror reusing, i.e. kfmclient tells running Konqueror instance
        to open new window
        - without focus stealing prevention - no problem
        - with ASN (application startup notification) - ASN is forwarded,
            and because it's newer than the instance's user timestamp,
            it takes precedence
        - without ASN - user timestamp needs to be reset, otherwise it would
            be used, and it's old; moreover this new window mustn't be detected
            as window belonging to already running application, or it wouldn't
            be activated - see x11::window::sameAppWindowRoleMatch() for the (rather ugly)
            hack
    - konqueror preloading, i.e. window is created in advance, and kfmclient
        tells this Konqueror instance to show it later
        - without focus stealing prevention - no problem
        - with ASN - ASN is forwarded, and because it's newer than the instance's
            user timestamp, it takes precedence
        - without ASN - user timestamp needs to be reset, otherwise it would
            be used, and it's old; also, creation timestamp is changed to
            the time the instance starts (re-)initializing the window,
            this ensures creation timestamp will still work somewhat even in this case
    - KUniqueApplication - when the window is already visible, and the new instance
        wants it to activate
        - without focus stealing prevention - _NET_ACTIVE_WINDOW - no problem
        - with ASN - ASN is forwarded, and set on the already visible window, KWin
            treats the window as new with that ASN
        - without ASN - _NET_ACTIVE_WINDOW as application request is used,
                and there's no really usable timestamp, only timestamp
                from the time the (new) application instance was started,
                so KWin will activate the window *sigh*
                - the bad thing here is that there's absolutely no chance to recognize
                    the case of starting this KUniqueApp from Konsole (and thus wanting
                    the already visible window to become active) from the case
                    when something started this KUniqueApp without ASN (in which case
                    the already visible window shouldn't become active)
                - the only solution is using ASN for starting applications, at least silent
                    (i.e. without feedback)
    - when one application wants to activate another application's window (e.g. KMail
        activating already running KAddressBook window ?)
        - without focus stealing prevention - _NET_ACTIVE_WINDOW - no problem
        - with ASN - can't be here, it's the KUniqueApp case then
        - without ASN - _NET_ACTIVE_WINDOW as application request should be used,
            KWin will activate the new window depending on the timestamp and
            whether it belongs to the currently active application

 _NET_ACTIVE_WINDOW usage:
 data.l[0]= 1 ->app request
          = 2 ->pager request
          = 0 - backwards compatibility
 data.l[1]= timestamp
*/

template<typename Space>
bool is_focus_change_allowed(Space& space)
{
    return space.block_focus == 0;
}

template<typename Space>
void cancel_delay_focus(Space& space)
{
    delete space.delayFocusTimer;
    space.delayFocusTimer = nullptr;
}

/**
 * Request focus and optionally try raising the window.
 *
 * Tries to activate the client by asking X for the input focus. This
 * function does not perform any show, raise or desktop switching. See
 * activate_window() instead.
 *
 * @see activate_window
 */
template<typename Space, typename Win>
void request_focus(Space& space, Win& window, bool raise = false, bool force_focus = false)
{
    using var_win = typename Space::window_t;

    auto window_ptr = &window;
    auto take_focus
        = is_focus_change_allowed(space) || var_win(window_ptr) == space.stacking.active;

    if (take_focus) {
        if constexpr (requires(Win win) { win.findModal(); }) {
            if (auto modal = window_ptr->findModal();
                modal && modal->control && modal != window_ptr) {
                if (auto desktop = get_desktop(*window_ptr); !on_desktop(modal, desktop)) {
                    set_desktop(modal, desktop);
                }
                if (!modal->isShown() && !modal->control->minimized) {
                    // forced desktop or utility window
                    // activating a minimized blocked window will unminimize its modal implicitly
                    activate_window(space, *modal);
                }
                // if the click was inside the window (i.e. handled is set),
                // but it has a modal, there's no need to use handled mode, because
                // the modal doesn't get the click anyway
                // raising of the original window needs to be still done
                if (raise) {
                    raise_window(space, window_ptr);
                }
                window_ptr = modal;
            }
        }
        cancel_delay_focus(space);
    }

    if (!force_focus && (is_dock(window_ptr) || is_splash(window_ptr))) {
        // toplevel menus and dock windows don't take focus if not forced
        // and don't have a flag that they take focus
        if constexpr (requires(Win win) { win.dockWantsInput(); }) {
            if (!window_ptr->dockWantsInput()) {
                take_focus = false;
            }
        } else {
            take_focus = false;
        }
    }

    if (!window_ptr->isShown()) {
        // Shouldn't happen, call activate_window() if needed.
        qCWarning(KWIN_CORE) << "request_focus: not shown";
        return;
    }

    if (take_focus) {
        window_ptr->takeFocus();
    }
    if (raise) {
        raise_window(space, window_ptr);
    }

    if (!on_active_screen(window_ptr)) {
        base::set_current_output(space.base, window_ptr->topo.central_output);
    }
}

/**
 * Puts the focus on a dummy window
 * Just using XSetInputFocus() with None would block keyboard input
 */
template<typename Space>
void focus_to_null(Space& space)
{
    if (space.m_nullFocus) {
        space.m_nullFocus->focus();
    }
}

template<typename Space>
std::optional<typename Space::window_t> window_under_mouse(Space const& space,
                                                           base::output const* output)
{
    auto it = space.stacking.order.stack.cend();

    while (it != space.stacking.order.stack.cbegin()) {
        auto window_var = *(--it);
        if (std::visit(overload{[&](auto&& window) {
                           if (!window->control) {
                               return false;
                           }

                           // Rule out windows which are not really visible.
                           // The screen test is rather superfluous for xrandr & twinview since the
                           // geometry would differ. -> TODO: might be dropped
                           if (!window->isShown() || !on_current_desktop(window)
                               || !on_screen(window, output)) {
                               return false;
                           }

                           return window->geo.frame.contains(space.input->cursor->pos());
                       }},
                       window_var)) {
            return window_var;
        }
    }

    return {};
}

template<typename Win>
void set_demands_attention(Win* win, bool demand)
{
    using var_win = typename Win::space_t::window_t;

    if (win->control->active) {
        demand = false;
    }
    if (win->control->demands_attention == demand) {
        return;
    }
    win->control->demands_attention = demand;

    if constexpr (requires(Win win) { win.net_info; }) {
        win->net_info->setState(demand ? NET::DemandsAttention : NET::States(),
                                NET::DemandsAttention);
    }

    remove_all(win->space.stacking.attention_chain, var_win(win));
    if (demand) {
        win->space.stacking.attention_chain.push_front(win);
    }

    Q_EMIT win->space.qobject->clientDemandsAttentionChanged(win->meta.signal_id, demand);
    Q_EMIT win->qobject->demandsAttentionChanged();
}

/**
 * Sets the client's active state to \a act.
 *
 * This function does only change the visual appearance of the client,
 * it does not change the focus setting. Use
 * Workspace::activateClient() or Workspace::requestFocus() instead.
 *
 * If a client receives or looses the focus, it calls setActive() on
 * its own.
 */
template<typename Win>
void set_active(Win* win, bool active)
{
    if (win->control->active == active) {
        return;
    }

    win->control->active = active;

    auto const ruledOpacity = active
        ? win->control->rules.checkOpacityActive(qRound(win->opacity() * 100.0))
        : win->control->rules.checkOpacityInactive(qRound(win->opacity() * 100.0));
    win->setOpacity(ruledOpacity / 100.0);

    if (active) {
        set_active_window(win->space, *win);
    } else {
        unset_active_window(win->space);
        win->control->cancel_auto_raise();
    }

    blocker block(win->space.stacking.order);

    // active windows may get different layer
    update_layer(win);

    auto leads = win->transient->leads();
    for (auto lead : leads) {
        if (lead->remnant) {
            continue;
        }
        if (lead->control->fullscreen) {
            // Fullscreens go high even if their transient is active.
            update_layer(lead);
        }
    }

    if constexpr (requires(Win win) { win.doSetActive(); }) {
        win->doSetActive();
    }
    Q_EMIT win->qobject->activeChanged();
    win->control->update_mouse_grab();
}

template<typename Space>
void unset_active_window(Space& space)
{
    auto& stacking = space.stacking;

    if (!stacking.active) {
        return;
    }

    if (space.active_popup && space.set_active_client_recursion == 0) {
        close_active_popup(space);
    }
    if (space.user_actions_menu->hasClient() && space.set_active_client_recursion == 0) {
        space.user_actions_menu->close();
    }

    blocker block(stacking.order);
    ++space.set_active_client_recursion;
    space.focusMousePos = space.input->cursor->pos();

    // note that this may call setActiveClient( NULL ), therefore the recursion counter
    std::visit(overload{[](auto&& win) { set_active(win, false); }}, *stacking.active);
    stacking.active = {};

    x11::update_tool_windows_visibility(&space, false);
    set_global_shortcuts_disabled(space, false);

    // e.g. fullscreens have different layer when active/not-active
    stacking.order.update_order();

    if (space.root_info) {
        x11::root_info_unset_active_window(*space.root_info);
    }

    Q_EMIT space.qobject->clientActivated();
    --space.set_active_client_recursion;
}

/**
 * Informs the space:: about the active client, i.e. the client that
 * has the focus (or None if no client has the focus). This functions
 * is called by the client itself that gets focus. It has no other
 * effect than fixing the focus chain and the return value of
 * activeClient(). And of course, to propagate the active client to the
 * world.
 */
template<typename Space, typename Win>
void set_active_window(Space& space, Win& window)
{
    using var_win = typename Space::window_t;
    auto& stacking = space.stacking;

    if (stacking.active == var_win(&window)) {
        return;
    }

    if (space.active_popup && space.active_popup_client != var_win(&window)
        && space.set_active_client_recursion == 0) {
        close_active_popup(space);
    }
    if (space.user_actions_menu->hasClient() && !space.user_actions_menu->isMenuClient(&window)
        && space.set_active_client_recursion == 0) {
        space.user_actions_menu->close();
    }

    blocker block(stacking.order);
    ++space.set_active_client_recursion;
    space.focusMousePos = space.input->cursor->pos();

    if (stacking.active) {
        // note that this may call setActiveClient( NULL ), therefore the recursion counter
        std::visit(overload{[](auto&& win) { set_active(win, false); }}, *stacking.active);
    }

    assert(window.control->active);
    stacking.active = &window;
    stacking.last_active = &window;

    focus_chain_update(stacking.focus_chain, &window, focus_chain_change::make_first);
    set_demands_attention(&window, false);

    // activating a client can cause a non active fullscreen window to loose the ActiveLayer
    // status on > 1 screens
    if (space.base.outputs.size() > 1) {
        for (auto win : space.windows) {
            auto check_win = [&window](auto candidate) {
                return candidate->control && get_layer(*candidate) == win::layer::active
                    && candidate->topo.central_output == window.topo.central_output;
            };
            std::visit(overload{[&](auto&& win) {
                                    if (check_win(win)) {
                                        update_layer(win);
                                    }
                                },
                                [&](Win* win) {
                                    if (win != &window && check_win(win)) {
                                        update_layer(win);
                                    }
                                }},
                       win);
        }
    }

    x11::update_tool_windows_visibility(&space, false);
    set_global_shortcuts_disabled(space, window.control->rules.checkDisableGlobalShortcuts(false));

    // e.g. fullscreens have different layer when active/not-active
    stacking.order.update_order();

    if constexpr (requires(Win win) { win.xcb_windows; }) {
        if (space.root_info) {
            x11::root_info_set_active_window(*space.root_info, window);
        }
    }

    Q_EMIT space.qobject->clientActivated();
    --space.set_active_client_recursion;
}

template<typename Space, typename Win>
void activate_window_impl(Space& space, Win& window, bool force)
{
    raise_window(space, &window);
    if (!on_current_desktop(&window)) {
        focus_blocker blocker(space);
        space.virtual_desktop_manager->setCurrent(get_desktop(window));
    }
    if (window.control->minimized) {
        set_minimized(&window, false);
    }

    // ensure the window is really visible - could eg. be a hidden utility window, see bug
    // #348083
    window.hideClient(false);

    // TODO force should perhaps allow this only if the window already contains the mouse
    if (kwinApp()->options->qobject->focusPolicyIsReasonable() || force) {
        request_focus(space, window, false, force);
    }

    if constexpr (requires(Win win) { win.handle_activated(); }) {
        window.handle_activated();
    }
}

template<typename Space>
void deactivate_window(Space& space)
{
    focus_to_null(space);
    unset_active_window(space);
}

template<typename Space, typename Win>
void activate_window(Space& space, Win& window)
{
    activate_window_impl(space, window, false);
}

template<typename Space, typename Win>
void force_activate_window(Space& space, Win& window)
{
    activate_window_impl(space, window, true);
}

template<typename Space>
void activate_attention_window(Space& space)
{
    if (!space.stacking.attention_chain.empty()) {
        std::visit(overload{[&](auto&& win) { activate_window(space, *win); }},
                   space.stacking.attention_chain.front());
    }
}

/// Deactivates current active window and activates next one.
template<typename Space>
bool activate_next_window(Space& space)
{
    auto prev_window = most_recently_activated_window(space);
    close_active_popup(space);

    if (prev_window) {
        if (prev_window == space.stacking.active) {
            unset_active_window(space);
        }
        auto& sgf = space.stacking.should_get_focus;
        sgf.erase(std::remove(sgf.begin(), sgf.end(), prev_window), sgf.end());
    }

    // if blocking focus, move focus to the desktop later if needed
    // in order to avoid flickering
    if (!is_focus_change_allowed(space)) {
        focus_to_null(space);
        return true;
    }

    if (!kwinApp()->options->qobject->focusPolicyIsReasonable()) {
        return false;
    }

    int const desktop = space.virtual_desktop_manager->current();

    if (space.showing_desktop) {
        // to not break the state
        if (auto desk_win = find_desktop(&space, true, desktop)) {
            std::visit(overload{[&](auto&& win) { request_focus(space, *win); }}, *desk_win);
            return true;
        }
    }

    typename Space::base_t::output_t const* output{nullptr};
    auto get_output = [&] {
        if (output) {
            return output;
        }
        if (prev_window) {
            return std::visit(overload{[](auto&& win) { return win->topo.central_output; }},
                              *prev_window);
        }
        return get_current_output(space);
    };

    if (kwinApp()->options->qobject->isNextFocusPrefersMouse()) {
        // Same as prev window and is_desktop should rather not happen.
        if (auto win = window_under_mouse(space, get_output());
            win && (!prev_window || *win != *prev_window)) {
            if (std::visit(overload{[&](auto&& win) {
                               if (is_desktop(win)) {
                                   return false;
                               }
                               request_focus(space, *win);
                               return true;
                           }},
                           *win)) {
                return true;
            }
            return true;
        }
    }

    // No suitable window under the mouse -> find sth. else.
    // First try to pass the focus to the (former) active clients leader.
    if (prev_window) {
        if (std::visit(overload{[&](auto&& prev_window) {
                           auto leaders = prev_window->transient->leads();
                           if (leaders.size() == 1
                               && focus_chain_is_usable_focus_candidate(
                                   space, *leaders.at(0), get_output())) {
                               auto win = leaders.front();

                               // Also raise - we don't know where it came from.
                               raise_window(space, win);
                               request_focus(space, *win);
                               return true;
                           }
                           return false;
                       }},
                       *prev_window)) {
            return true;
        }
    }

    // Ask the focus chain for the next candidate.
    if (auto win = focus_chain_next(space, prev_window, desktop, get_output())) {
        std::visit(overload{[&](auto&& win) { request_focus(space, *win); }}, *win);
        return true;
    }

    // last chance: focus the desktop
    if (auto win = find_desktop(&space, true, desktop)) {
        std::visit(overload{[&](auto&& win) { request_focus(space, *win); }}, *win);
        return true;
    }

    focus_to_null(space);
    return true;
}

/**
 * Informs the space that the \a window has been hidden. If it was the active window (or to-become
 * the active window), the space activates another one.
 *
 * @note @p window may already be destroyed.
 */
template<typename Space, typename Win>
void process_window_hidden(Space& space, Win& window)
{
    using var_win = typename Space::window_t;
    assert(!window.isShown() || !on_current_desktop(&window));
    if (most_recently_activated_window(space) == var_win(&window)) {
        activate_next_window(space);
    }
}

template<typename Space>
std::optional<typename Space::window_t> find_window_to_activate_on_desktop(Space& space,
                                                                           unsigned int desktop)

{
    auto& stacking = space.stacking;

    if (space.move_resize_window && stacking.active == space.move_resize_window
        && focus_chain_at_desktop_contains(stacking.focus_chain, *stacking.active, desktop)
        && std::visit(
            overload{[&](auto&& win) { return win->isShown() && on_current_desktop(win); }},
            *stacking.active)) {
        // A requestFocus call will fail, as the client is already active
        return stacking.active;
    }

    // from actiavtion.cpp
    if (kwinApp()->options->qobject->isNextFocusPrefersMouse()) {
        auto it = stacking.order.stack.cend();
        while (it != stacking.order.stack.cbegin()) {
            if (auto win = std::visit(
                    overload{[&](auto&& win) -> std::optional<typename Space::window_t> {
                        if (!win->control) {
                            return {};
                        }

                        if (!(win->isShown() && on_desktop(win, desktop) && on_active_screen(win)))
                            return {};

                        if (win->geo.frame.contains(space.input->cursor->pos())) {
                            if (!is_desktop(win)) {
                                return win;
                            }
                            // Stop. We don't pass focus to some window below an unusable one.
                            it = stacking.order.stack.cbegin();
                        }
                        return {};
                    }},
                    *(--it))) {
                return win;
            }
        }
    }

    return focus_chain_get_for_activation_on_current_output(space, desktop);
}

template<typename Space>
void activate_window_on_new_desktop(Space& space, unsigned int desktop)
{
    using var_win = typename Space::window_t;
    auto& stacking = space.stacking;

    auto do_activate = [&](auto& win) {
        std::visit(overload{[&](auto&& win) {
                       if (var_win(win) != stacking.active) {
                           unset_active_window(space);
                       }
                       request_focus(space, *win);
                   }},
                   win);
    };

    if (kwinApp()->options->qobject->focusPolicyIsReasonable()) {
        if (auto win = find_window_to_activate_on_desktop(space, desktop)) {
            do_activate(*win);
            return;
        }
    } else if (stacking.active
               && std::visit(
                   overload{[&](auto&& win) { return win->isShown() && on_current_desktop(win); }},
                   *stacking.active)) {
        // If "unreasonable focus policy" and stacking.active is on_all_desktops and
        // under mouse (Hence == stacking.last_active), conserve focus.
        // (Thanks to Volker Schatz <V.Schatz at thphys.uni-heidelberg.de>)
        do_activate(*stacking.active);
        return;
    }

    if (auto win = find_desktop(&space, true, desktop)) {
        do_activate(*win);
        return;
    }

    focus_to_null(space);
}

template<typename Space, typename Win>
bool activate_window_direction(Space& space,
                               Win& window,
                               win::direction direction,
                               QPoint curPos,
                               int desktop)
{
    using var_win = typename Space::window_t;

    std::optional<var_win> next_window;
    int bestScore = 0;
    auto const& stack = space.stacking.order.stack;

    for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
        std::visit(overload{[&](auto&& win) {
                       if constexpr (std::is_same_v<std::decay_t<decltype(win)>, Win>) {
                           if (win == &window) {
                               return;
                           }
                       }
                       if (!win->control || !wants_tab_focus(win) || !on_desktop(win, desktop)
                           || win->control->minimized) {
                           return;
                       }

                       // Centre of the other window
                       auto const other = QPoint(win->geo.pos().x() + win->geo.size().width() / 2,
                                                 win->geo.pos().y() + win->geo.size().height() / 2);
                       int distance;
                       int offset;

                       switch (direction) {
                       case direction::north:
                           distance = curPos.y() - other.y();
                           offset = qAbs(other.x() - curPos.x());
                           break;
                       case direction::east:
                           distance = other.x() - curPos.x();
                           offset = qAbs(other.y() - curPos.y());
                           break;
                       case direction::south:
                           distance = other.y() - curPos.y();
                           offset = qAbs(other.x() - curPos.x());
                           break;
                       case direction::west:
                           distance = curPos.x() - other.x();
                           offset = qAbs(other.y() - curPos.y());
                           break;
                       default:
                           distance = -1;
                           offset = -1;
                       }

                       if (distance <= 0) {
                           return;
                       }

                       // Inverse score
                       int score = distance + offset + ((offset * offset) / distance);
                       if (score < bestScore || !next_window) {
                           next_window = win;
                           bestScore = score;
                       }
                   }},
                   *it);
    }

    if (!next_window) {
        return false;
    }

    std::visit(overload{[&](auto&& win) { activate_window(space, *win); }}, *next_window);
    return true;
}

/**
 * Switches to the nearest window in given direction.
 */
template<typename Space>
void activate_window_direction(Space& space, win::direction direction)
{
    if (!space.stacking.active) {
        return;
    }

    std::visit(
        overload{[&](auto&& act_win) {
            int desktopNumber = on_all_desktops(act_win) ? space.virtual_desktop_manager->current()
                                                         : get_desktop(*act_win);

            // Centre of the active window
            auto curPos = QPoint(act_win->geo.pos().x() + act_win->geo.size().width() / 2,
                                 act_win->geo.pos().y() + act_win->geo.size().height() / 2);

            if (activate_window_direction(space, *act_win, direction, curPos, desktopNumber)) {
                return;
            }

            auto opposite = [&] {
                switch (direction) {
                case direction::north:
                    return QPoint(curPos.x(), kwinApp()->get_base().topology.size.height());
                case direction::south:
                    return QPoint(curPos.x(), 0);
                case direction::east:
                    return QPoint(0, curPos.y());
                case direction::west:
                    return QPoint(kwinApp()->get_base().topology.size.width(), curPos.y());
                default:
                    Q_UNREACHABLE();
                }
            };

            activate_window_direction(space, *act_win, direction, opposite(), desktopNumber);
        }},
        *space.stacking.active);
}

template<typename Space>
void delay_focus(Space& space)
{
    if (auto delay = space.stacking.delayfocus_window) {
        std::visit(overload{[&](auto&& delay) { request_focus(space, *delay); }}, *delay);
    } else {
        focus_to_null(space);
    }
    cancel_delay_focus(space);
}

template<typename Space>
void reset_delay_focus_timer(Space& space)
{
    delete space.delayFocusTimer;
    space.delayFocusTimer = new QTimer(space.qobject.get());

    QObject::connect(space.delayFocusTimer, &QTimer::timeout, space.qobject.get(), [&space] {
        delay_focus(space);
    });
    space.delayFocusTimer->setSingleShot(true);
    space.delayFocusTimer->start(kwinApp()->options->qobject->delayFocusInterval());
}

template<typename Space>
void close_active_popup(Space& space)
{
    if (space.active_popup) {
        space.active_popup->close();
        space.active_popup = nullptr;
        space.active_popup_client = {};
    }

    space.user_actions_menu->close();
}

template<typename Space>
void set_showing_desktop(Space& space, bool showing)
{
    const bool changed = showing != space.showing_desktop;
    if (space.root_info && changed) {
        space.root_info->setShowingDesktop(showing);
    }

    space.showing_desktop = showing;

    std::optional<typename Space::window_t> topDesk;

    // For the blocker RAII, updateLayer & lowerClient would invalidate stacking.order.
    {
        blocker block(space.stacking.order);
        for (int i = static_cast<int>(space.stacking.order.stack.size()) - 1; i > -1; --i) {
            std::visit(overload{[&](auto&& win) {
                           if (!on_current_desktop(win)) {
                               return;
                           }
                           if (is_dock(win)) {
                               update_layer(win);
                               return;
                           }

                           if (!is_desktop(win) || !win->isShown()) {
                               return;
                           }

                           update_layer(win);
                           lower_window(space, win);

                           if (!topDesk) {
                               topDesk = win;
                           }

                           for (auto relative : get_transient_family(win)) {
                               update_layer(relative);
                           }
                       }},
                       space.stacking.order.stack.at(i));
        }
    } // ~Blocker

    if (space.showing_desktop && topDesk) {
        std::visit(overload{[&](auto&& win) { request_focus(space, *win); }}, *topDesk);
    } else if (!space.showing_desktop && changed) {
        if (auto const window = focus_chain_get_for_activation_on_current_output(
                space, space.virtual_desktop_manager->current())) {
            std::visit(overload{[&](auto&& win) { activate_window(space, *win); }}, *window);
        }
    }
    if (changed) {
        Q_EMIT space.qobject->showingDesktopChanged(showing);
    }
}

template<typename Space>
void toggle_show_desktop(Space& space)
{
    set_showing_desktop(space, !space.showing_desktop);
}
}
