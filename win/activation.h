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
template<typename Space>
void request_focus(Space& space,
                   typename Space::window_t* window,
                   bool raise = false,
                   bool force_focus = false)
{
    auto take_focus = is_focus_change_allowed(space) || window == space.active_client;

    if (!window) {
        focus_to_null(space);
        return;
    }

    if (take_focus) {
        auto modal = window->findModal();
        if (modal && modal->control && modal != window) {
            if (!modal->isOnDesktop(window->desktop())) {
                set_desktop(modal, window->desktop());
            }
            if (!modal->isShown() && !modal->control->minimized) {
                // forced desktop or utility window
                // activating a minimized blocked window will unminimize its modal implicitly
                activate_window(space, modal);
            }
            // if the click was inside the window (i.e. handled is set),
            // but it has a modal, there's no need to use handled mode, because
            // the modal doesn't get the click anyway
            // raising of the original window needs to be still done
            if (raise) {
                raise_window(&space, window);
            }
            window = modal;
        }
        cancel_delay_focus(space);
    }

    if (!force_focus && (is_dock(window) || is_splash(window))) {
        // toplevel menus and dock windows don't take focus if not forced
        // and don't have a flag that they take focus
        if (!window->dockWantsInput()) {
            take_focus = false;
        }
    }

    if (!window->isShown()) {
        // Shouldn't happen, call activate_window() if needed.
        qCWarning(KWIN_CORE) << "request_focus: not shown";
        return;
    }

    if (take_focus) {
        window->takeFocus();
    }
    if (raise) {
        raise_window(&space, window);
    }

    if (!on_active_screen(window)) {
        base::set_current_output(space.base, window->central_output);
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
typename Space::window_t* window_under_mouse(Space const& space, base::output const* output)
{
    auto it = space.stacking_order->stack.cend();

    while (it != space.stacking_order->stack.cbegin()) {
        auto window = *(--it);
        if (!window->control) {
            continue;
        }

        // Rule out windows which are not really visible.
        // The screen test is rather superfluous for xrandr & twinview since the geometry would
        // differ. -> TODO: might be dropped
        if (!(window->isShown() && window->isOnCurrentDesktop() && on_screen(window, output)))
            continue;

        if (window->frameGeometry().contains(space.input->platform.cursor->pos())) {
            return window;
        }
    }

    return nullptr;
}

template<typename Win>
void set_demands_attention(Win* win, bool demand)
{
    if (win->control->active) {
        demand = false;
    }
    if (win->control->demands_attention == demand) {
        return;
    }
    win->control->demands_attention = demand;

    if (win->info) {
        win->info->setState(demand ? NET::DemandsAttention : NET::States(), NET::DemandsAttention);
    }

    remove_all(win->space.attention_chain, win);
    if (demand) {
        win->space.attention_chain.push_front(win);
    }

    Q_EMIT win->space.qobject->clientDemandsAttentionChanged(win->signal_id, demand);
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

    set_active_window(win->space, active ? win : nullptr);

    if (!active) {
        win->control->cancel_auto_raise();
    }

    blocker block(win->space.stacking_order);

    // active windows may get different layer
    update_layer(win);

    auto leads = win->transient()->leads();
    for (auto lead : leads) {
        if (lead->remnant) {
            continue;
        }
        if (lead->control->fullscreen) {
            // Fullscreens go high even if their transient is active.
            update_layer(lead);
        }
    }

    win->doSetActive();
    Q_EMIT win->qobject->activeChanged();
    win->control->update_mouse_grab();
}

/**
 * Informs the space:: about the active client, i.e. the client that
 * has the focus (or None if no client has the focus). This functions
 * is called by the client itself that gets focus. It has no other
 * effect than fixing the focus chain and the return value of
 * activeClient(). And of course, to propagate the active client to the
 * world.
 */
template<typename Space>
void set_active_window(Space& space, typename Space::window_t* window)
{
    if (space.active_client == window)
        return;

    if (space.active_popup && space.active_popup_client != window
        && space.set_active_client_recursion == 0) {
        close_active_popup(space);
    }
    if (space.user_actions_menu->hasClient() && !space.user_actions_menu->isMenuClient(window)
        && space.set_active_client_recursion == 0) {
        space.user_actions_menu->close();
    }

    blocker block(space.stacking_order);
    ++space.set_active_client_recursion;
    space.focusMousePos = space.input->platform.cursor->pos();

    if (space.active_client != nullptr) {
        // note that this may call setActiveClient( NULL ), therefore the recursion counter
        set_active(space.active_client, false);
    }

    space.active_client = window;
    assert(!window || window->control->active);

    if (space.active_client) {
        space.last_active_client = space.active_client;
        focus_chain_update(space.focus_chain, space.active_client, focus_chain_change::make_first);
        set_demands_attention(space.active_client, false);

        // activating a client can cause a non active fullscreen window to loose the ActiveLayer
        // status on > 1 screens
        if (space.base.outputs.size() > 1) {
            for (auto win : space.windows) {
                if (win->control && win != space.active_client && win->layer() == win::layer::active
                    && win->central_output == space.active_client->central_output) {
                    update_layer(win);
                }
            }
        }
    }

    x11::update_tool_windows_visibility(&space, false);
    if (window) {
        set_global_shortcuts_disabled(space,
                                      window->control->rules.checkDisableGlobalShortcuts(false));
    } else {
        set_global_shortcuts_disabled(space, false);
    }

    // e.g. fullscreens have different layer when active/not-active
    space.stacking_order->update_order();

    if (space.root_info) {
        x11::root_info_set_active_window(*space.root_info, space.active_client);
    }

    Q_EMIT space.qobject->clientActivated();
    --space.set_active_client_recursion;
}

template<typename Space, typename Win>
void activate_window_impl(Space& space, Win* window, bool force)
{
    if (window == nullptr) {
        focus_to_null(space);
        set_active_window(space, nullptr);
        return;
    }
    raise_window(&space, window);
    if (!window->isOnCurrentDesktop()) {
        focus_blocker blocker(space);
        space.virtual_desktop_manager->setCurrent(window->desktop());
    }
    if (window->control->minimized) {
        set_minimized(window, false);
    }

    // ensure the window is really visible - could eg. be a hidden utility window, see bug
    // #348083
    window->hideClient(false);

    // TODO force should perhaps allow this only if the window already contains the mouse
    if (kwinApp()->options->qobject->focusPolicyIsReasonable() || force) {
        request_focus(space, window, false, force);
    }

    window->handle_activated();
}

template<typename Space>
void activate_window(Space& space, typename Space::window_t* window)
{
    activate_window_impl(space, window, false);
}

template<typename Space, typename Win>
void force_activate_window(Space& space, Win* window)
{
    activate_window_impl(space, window, true);
}

template<typename Space>
void activate_attention_window(Space& space)
{
    if (space.attention_chain.size() > 0) {
        activate_window(space, space.attention_chain.front());
    }
}

/// Deactivates 'window' and activates next one.
template<typename Space>
bool activate_next_window(Space& space, typename Space::window_t* window)
{
    // If 'window' is not the active or the to-become active one, do nothing.
    if (!(window == space.active_client
          || (space.should_get_focus.size() > 0 && window == space.should_get_focus.back()))) {
        return false;
    }

    close_active_popup(space);

    if (window != nullptr) {
        if (window == space.active_client) {
            set_active_window(space, nullptr);
        }
        space.should_get_focus.erase(
            std::remove(space.should_get_focus.begin(), space.should_get_focus.end(), window),
            space.should_get_focus.end());
    }

    // if blocking focus, move focus to the desktop later if needed
    // in order to avoid flickering
    if (!is_focus_change_allowed(space)) {
        focus_to_null(space);
        return true;
    }

    if (!kwinApp()->options->qobject->focusPolicyIsReasonable())
        return false;

    decltype(window) get_focus = nullptr;

    int const desktop = space.virtual_desktop_manager->current();

    if (!get_focus && space.showing_desktop) {
        // to not break the state
        get_focus = find_desktop(&space, true, desktop);
    }

    if (!get_focus && kwinApp()->options->qobject->isNextFocusPrefersMouse()) {
        get_focus = window_under_mouse(space,
                                       window ? window->central_output : get_current_output(space));
        if (get_focus && (get_focus == window || is_desktop(get_focus))) {
            // should rather not happen, but it cannot get the focus. rest of usability is tested
            // above
            get_focus = nullptr;
        }
    }

    if (!get_focus) { // no suitable window under the mouse -> find sth. else
        // first try to pass the focus to the (former) active clients leader
        if (window && window->transient()->lead()) {
            auto leaders = window->transient()->leads();
            if (leaders.size() == 1
                && focus_chain_is_usable_focus_candidate(
                    space.focus_chain, leaders.at(0), window)) {
                get_focus = leaders.at(0);

                // also raise - we don't know where it came from
                win::raise_window(&space, get_focus);
            }
        }
        if (!get_focus) {
            // nope, ask the focus chain for the next candidate
            get_focus = focus_chain_next_for_desktop(space.focus_chain, window, desktop);
        }
    }

    if (!get_focus) {
        // last chance: focus the desktop
        get_focus = find_desktop(&space, true, desktop);
    }

    if (get_focus) {
        request_focus(space, get_focus);
    } else {
        focus_to_null(space);
    }

    return true;
}

/**
 * Informs the space that the \a window has been hidden. If it was the active window (or to-become
 * the active window), the space activates another one.
 *
 * @note @p window may already be destroyed.
 */
template<typename Space, typename Win>
void process_window_hidden(Space& space, Win* window)
{
    assert(!window->isShown() || !window->isOnCurrentDesktop());
    activate_next_window(space, window);
}

template<typename Space>
typename Space::window_t* find_window_to_activate_on_desktop(Space& space, unsigned int desktop)

{
    if (space.move_resize_window && space.active_client == space.move_resize_window
        && focus_chain_at_desktop_contains(space.focus_chain, space.active_client, desktop)
        && space.active_client->isShown() && space.active_client->isOnCurrentDesktop()) {
        // A requestFocus call will fail, as the client is already active
        return space.active_client;
    }

    // from actiavtion.cpp
    if (kwinApp()->options->qobject->isNextFocusPrefersMouse()) {
        auto it = space.stacking_order->stack.cend();
        while (it != space.stacking_order->stack.cbegin()) {
            auto window = *(--it);
            if (!window->control) {
                continue;
            }

            if (!(window->isShown() && window->isOnDesktop(desktop) && on_active_screen(window)))
                continue;

            if (window->frameGeometry().contains(space.input->platform.cursor->pos())) {
                if (!is_desktop(window)) {
                    return window;
                }
                // Unconditional break, we don't pass focus to some window below an unusable one.
                break;
            }
        }
    }

    return focus_chain_get_for_activation_on_current_output<typename Space::window_t>(
        space.focus_chain, desktop);
}

template<typename Space>
void activate_window_on_new_desktop(Space& space, unsigned int desktop)
{
    typename Space::window_t* c = nullptr;

    if (kwinApp()->options->qobject->focusPolicyIsReasonable()) {
        c = find_window_to_activate_on_desktop(space, desktop);
    }

    // If "unreasonable focus policy" and active_client is on_all_desktops and
    // under mouse (Hence == old_active_client), conserve focus.
    // (Thanks to Volker Schatz <V.Schatz at thphys.uni-heidelberg.de>)
    else if (space.active_client && space.active_client->isShown()
             && space.active_client->isOnCurrentDesktop()) {
        c = space.active_client;
    }

    if (!c) {
        c = find_desktop(&space, true, desktop);
    }

    if (c != space.active_client) {
        set_active_window(space, nullptr);
    }

    if (c) {
        request_focus(space, c);
    } else if (auto desktop_client = find_desktop(&space, true, desktop)) {
        request_focus(space, desktop_client);
    } else {
        focus_to_null(space);
    }
}

template<typename Space>
bool activate_window_direction(Space& space,
                               typename Space::window_t* c,
                               win::direction direction,
                               QPoint curPos,
                               int d)
{
    decltype(c) switchTo = nullptr;
    int bestScore = 0;
    auto clist = space.stacking_order->stack;

    for (auto i = clist.rbegin(); i != clist.rend(); ++i) {
        auto client = *i;
        if (!client->control) {
            continue;
        }

        if (wants_tab_focus(client) && *i != c && client->isOnDesktop(d)
            && !client->control->minimized) {
            // Centre of the other window
            const QPoint other(client->pos().x() + client->size().width() / 2,
                               client->pos().y() + client->size().height() / 2);

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

            if (distance > 0) {
                // Inverse score
                int score = distance + offset + ((offset * offset) / distance);
                if (score < bestScore || !switchTo) {
                    switchTo = client;
                    bestScore = score;
                }
            }
        }
    }
    if (switchTo) {
        activate_window(space, switchTo);
    }

    return switchTo;
}

/**
 * Switches to the nearest window in given direction.
 */
template<typename Space>
void activate_window_direction(Space& space, win::direction direction)
{
    if (!space.active_client) {
        return;
    }

    auto c = space.active_client;
    int desktopNumber
        = c->isOnAllDesktops() ? space.virtual_desktop_manager->current() : c->desktop();

    // Centre of the active window
    QPoint curPos(c->pos().x() + c->size().width() / 2, c->pos().y() + c->size().height() / 2);

    if (!activate_window_direction(space, c, direction, curPos, desktopNumber)) {
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

        activate_window_direction(space, c, direction, opposite(), desktopNumber);
    }
}

template<typename Space>
void delay_focus(Space& space)
{
    request_focus(space, space.delayfocus_client);
    cancel_delay_focus(space);
}

template<typename Space>
void request_delay_focus(Space& space, typename Space::window_t* c)
{
    space.delayfocus_client = c;

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
        space.active_popup_client = nullptr;
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

    typename Space::window_t* topDesk = nullptr;

    // for the blocker RAII
    // updateLayer & lowerClient would invalidate stacking_order
    {
        blocker block(space.stacking_order);
        for (int i = static_cast<int>(space.stacking_order->stack.size()) - 1; i > -1; --i) {
            auto c = space.stacking_order->stack.at(i);
            if (c->isOnCurrentDesktop()) {
                if (is_dock(c)) {
                    update_layer(c);
                } else if (is_desktop(c) && c->isShown()) {
                    update_layer(c);
                    lower_window(&space, c);
                    if (!topDesk)
                        topDesk = c;
                    for (auto cm : get_transient_family(c)) {
                        update_layer(cm);
                    }
                }
            }
        }
    } // ~Blocker

    if (space.showing_desktop && topDesk) {
        request_focus(space, topDesk);
    } else if (!space.showing_desktop && changed) {
        auto const window
            = focus_chain_get_for_activation_on_current_output<typename Space::window_t>(
                space.focus_chain, space.virtual_desktop_manager->current());
        if (window) {
            activate_window(space, window);
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
