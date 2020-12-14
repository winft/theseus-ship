/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

/*

 This file contains things relevant to window activation and focus
 stealing prevention.

*/

#include "cursor.h"
#include "focuschain.h"
#include "netinfo.h"
#include "workspace.h"
#ifdef KWIN_BUILD_ACTIVITIES
#include "activities.h"
#endif

#include <kstartupinfo.h>
#include <kstringhandler.h>
#include <KLocalizedString>

#include "atoms.h"
#include "group.h"
#include "rules/rules.h"
#include "screens.h"
#include "useractions.h"

#include "win/space.h"
#include "win/util.h"
#include "win/x11/control.h"
#include "win/x11/window.h"

#include <QDebug>

namespace KWin
{

/*
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
   Workspace::allowClientActivation() (see below).
 - a new window will be mapped - this is the most complicated case. If
   the new window belongs to the currently active application, it may be safely
   mapped on top and activated. The same if there's no active window,
   or the active window is the desktop. These checks are done by
   Workspace::allowClientActivation().
    Following checks need to compare times. One time is the timestamp
   of last user action in the currently active window, the other time is
   the timestamp of the action that originally caused mapping of the new window
   (e.g. when the application was started). If the first time is newer than
   the second one, the window will not be activated, as that indicates
   futher user actions took place after the action leading to this new
   mapped window. This check is done by Workspace::allowClientActivation().
    There are several ways how to get the timestamp of action that caused
   the new mapped window (done in win::x11::window::readUserTimeMapTimestamp()) :
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
   process) is done in win::x11::window::belongToSameApplication(). Not 100% reliable,
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
            be activated - see win::x11::window::sameAppWindowRoleMatch() for the (rather ugly)
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


//****************************************
// Workspace
//****************************************


/**
 * Informs the workspace about the active client, i.e. the client that
 * has the focus (or None if no client has the focus). This functions
 * is called by the client itself that gets focus. It has no other
 * effect than fixing the focus chain and the return value of
 * activeClient(). And of course, to propagate the active client to the
 * world.
 */
void Workspace::setActiveClient(Toplevel *window)
{
    if (active_client == window)
        return;

    if (active_popup && active_popup_client != window && set_active_client_recursion == 0)
        closeActivePopup();
    if (m_userActionsMenu->hasClient() && !m_userActionsMenu->isMenuClient(window) && set_active_client_recursion == 0) {
        m_userActionsMenu->close();
    }

    StackingUpdatesBlocker blocker(this);
    ++set_active_client_recursion;
    updateFocusMousePosition(Cursor::pos());
    if (active_client != nullptr) {
        // note that this may call setActiveClient( NULL ), therefore the recursion counter
        win::set_active(active_client, false);
    }
    active_client = window;

    Q_ASSERT(window == nullptr || window->control->active());

    if (active_client) {
        last_active_client = active_client;
        FocusChain::self()->update(active_client, FocusChain::MakeFirst);
        win::set_demands_attention(active_client, false);

        // activating a client can cause a non active fullscreen window to loose the ActiveLayer status on > 1 screens
        if (screens()->count() > 1) {
            for (auto it = m_allClients.begin(); it != m_allClients.end(); ++it) {
                if (*it != active_client && (*it)->layer() == win::layer::active
                        && (*it)->screen() == active_client->screen()) {
                    updateClientLayer(*it);
                }
            }
        }
    }

    win::update_tool_windows(this, false);
    if (window)
        disableGlobalShortcutsForClient(window->control->rules().checkDisableGlobalShortcuts(false));
    else
        disableGlobalShortcutsForClient(false);

    updateStackingOrder(); // e.g. fullscreens have different layer when active/not-active

    if (rootInfo()) {
        rootInfo()->setActiveClient(active_client);
    }

    emit clientActivated(active_client);
    --set_active_client_recursion;
}

/**
 * Tries to activate the client \a c. This function performs what you
 * expect when clicking the respective entry in a taskbar: showing and
 * raising the client (this may imply switching to the another virtual
 * desktop) and putting the focus onto it. Once X really gave focus to
 * the client window as requested, the client itself will call
 * setActiveClient() and the operation is complete. This may not happen
 * with certain focus policies, though.
 *
 * @see setActiveClient
 * @see requestFocus
 */
void Workspace::activateClient(Toplevel *window, bool force)
{
    if (window == nullptr) {
        focusToNull();
        setActiveClient(nullptr);
        return;
    }
    raise_window(window);
    if (!window->isOnCurrentDesktop()) {
        ++block_focus;
        VirtualDesktopManager::self()->setCurrent(window->desktop());
        --block_focus;
    }
#ifdef KWIN_BUILD_ACTIVITIES
    if (!window->isOnCurrentActivity()) {
        ++block_focus;
        //DBUS!
        //first isn't necessarily best, but it's easiest
        Activities::self()->setCurrent(window->activities().first());
        --block_focus;
    }
#endif
    if (window->control->minimized()) {
        win::set_minimized(window, false);
    }

    // ensure the window is really visible - could eg. be a hidden utility window, see bug #348083
    window->hideClient(false);

// TODO force should perhaps allow this only if the window already contains the mouse
    if (options->focusPolicyIsReasonable() || force) {
        request_focus(window, false, force);
    }

    // Don't update user time for clients that have focus stealing workaround.
    // As they usually belong to the current active window but fail to provide
    // this information, updating their user time would make the user time
    // of the currently active window old, and reject further activation for it.
    // E.g. typing URL in minicli which will show kio_uiserver dialog (with workaround),
    // and then kdesktop shows dialog about SSL certificate.
    // This needs also avoiding user creation time in win::x11::window::readUserTimeMapTimestamp().
    if (auto client = dynamic_cast<win::x11::window*>(window)) {
        // updateUserTime is X11 specific
        win::x11::update_user_time(client);
    }
}

/**
 * Tries to activate the client by asking X for the input focus. This
 * function does not perform any show, raise or desktop switching. See
 * Workspace::activateClient() instead.
 *
 * @see activateClient
 */
void Workspace::request_focus(Toplevel *window, bool raise, bool force_focus)
{
    auto take_focus = focusChangeEnabled() || window == active_client;

    if (!window) {
        focusToNull();
        return;
    }

    if (take_focus) {
        auto modal = window->findModal();
        if (modal && modal->control && modal != window) {
            if (!modal->isOnDesktop(window->desktop())) {
                win::set_desktop(modal, window->desktop());
            }
            if (!modal->isShown(true) && !modal->control->minimized()) {
                // forced desktop or utility window
                // activating a minimized blocked window will unminimize its modal implicitly
                activateClient(modal);
            }
            // if the click was inside the window (i.e. handled is set),
            // but it has a modal, there's no need to use handled mode, because
            // the modal doesn't get the click anyway
            // raising of the original window needs to be still done
            if (raise) {
                raise_window(window);
            }
            window = modal;
        }
        cancelDelayFocus();
    }
    if (!force_focus && (win::is_dock(window) || win::is_splash(window))) {
        // toplevel menus and dock windows don't take focus if not forced
        // and don't have a flag that they take focus
        if (!window->dockWantsInput()) {
            take_focus = false;
        }
    }
    if (win::shaded(window)) {
        if (window->wantsInput() && take_focus) {
            // client cannot accept focus, but at least the window should be active (window menu, et. al. )
            win::set_active(window, true);
            focusToNull();
        }
        take_focus = false;
    }
    if (!window->isShown(true)) {  // shouldn't happen, call activateClient() if needed
        qCWarning(KWIN_CORE) << "request_focus: not shown" ;
        return;
    }

    if (take_focus) {
        window->takeFocus();
    }
    if (raise) {
        workspace()->raise_window(window);
    }

    if (!win::on_active_screen(window)) {
        screens()->setCurrent(window->screen());
    }
}

/**
 * Informs the workspace that the client \a c has been hidden. If it
 * was the active client (or to-become the active client),
 * the workspace activates another one.
 *
 * @note @p c may already be destroyed.
 */
void Workspace::clientHidden(Toplevel* window)
{
    Q_ASSERT(!window->isShown(true) || !window->isOnCurrentDesktop() || !window->isOnCurrentActivity());
    activateNextClient(window);
}

Toplevel* Workspace::clientUnderMouse(int screen) const
{
    auto it = stackingOrder().cend();
    while (it != stackingOrder().cbegin()) {
        auto client = *(--it);
        if (!client->control) {
            continue;
        }

        // rule out clients which are not really visible.
        // the screen test is rather superfluous for xrandr & twinview since the geometry would differ -> TODO: might be dropped
        if (!(client->isShown(false) && client->isOnCurrentDesktop() &&
                client->isOnCurrentActivity() && win::on_screen(client, screen)))
            continue;

        if (client->frameGeometry().contains(Cursor::pos())) {
            return client;
        }
    }
    return nullptr;
}

// deactivates 'c' and activates next client
bool Workspace::activateNextClient(Toplevel* window)
{
    // if 'c' is not the active or the to-become active one, do nothing
    if (!(window == active_client ||
          (should_get_focus.size() > 0 && window == should_get_focus.back()))) {
        return false;
    }

    closeActivePopup();

    if (window != nullptr) {
        if (window == active_client) {
            setActiveClient(nullptr);
        }
        should_get_focus.erase(std::remove(should_get_focus.begin(), should_get_focus.end(), window),
                               should_get_focus.end());
    }

    // if blocking focus, move focus to the desktop later if needed
    // in order to avoid flickering
    if (!focusChangeEnabled()) {
        focusToNull();
        return true;
    }

    if (!options->focusPolicyIsReasonable())
        return false;

    Toplevel* get_focus = nullptr;

    const int desktop = VirtualDesktopManager::self()->current();

    if (!get_focus && showingDesktop())
        get_focus = findDesktop(true, desktop); // to not break the state

    if (!get_focus && options->isNextFocusPrefersMouse()) {
        get_focus = clientUnderMouse(window ? window->screen() : screens()->current());
        if (get_focus && (get_focus == window || win::is_desktop(get_focus))) {
            // should rather not happen, but it cannot get the focus. rest of usability is tested above
            get_focus = nullptr;
        }
    }

    if (!get_focus) { // no suitable window under the mouse -> find sth. else
        // first try to pass the focus to the (former) active clients leader
        if (window && window->isTransient()) {
            auto leaders = window->transient()->leads();
            if (leaders.size() == 1 &&
                    FocusChain::self()->isUsableFocusCandidate(leaders.at(0), window)) {
                get_focus = leaders.at(0);

                // also raise - we don't know where it came from
                raise_window(get_focus);
            }
        }
        if (!get_focus) {
            // nope, ask the focus chain for the next candidate
            get_focus = FocusChain::self()->nextForDesktop(window, desktop);
        }
    }

    if (get_focus == nullptr)   // last chance: focus the desktop
        get_focus = findDesktop(true, desktop);

    if (get_focus != nullptr) {
        request_focus(get_focus);
    } else {
        focusToNull();
    }

    return true;

}

void Workspace::setCurrentScreen(int new_screen)
{
    if (new_screen < 0 || new_screen >= screens()->count())
        return;
    if (!options->focusPolicyIsReasonable())
        return;
    closeActivePopup();
    const int desktop = VirtualDesktopManager::self()->current();
    auto    get_focus = FocusChain::self()->getForActivation(desktop, new_screen);
    if (get_focus == nullptr) {
        get_focus = findDesktop(true, desktop);
    }
    if (get_focus != nullptr && get_focus != mostRecentlyActivatedClient()) {
        request_focus(get_focus);
    }
    screens()->setCurrent(new_screen);
}

void Workspace::gotFocusIn(Toplevel const* window)
{
    if (std::find(should_get_focus.cbegin(), should_get_focus.cend(),
                  const_cast<Toplevel*>(window)) != should_get_focus.cend()) {
        // remove also all sooner elements that should have got FocusIn,
        // but didn't for some reason (and also won't anymore, because they were sooner)
        while (should_get_focus.front() != window) {
            should_get_focus.pop_front();
        }
        should_get_focus.pop_front(); // remove 'c'
    }
}

void Workspace::setShouldGetFocus(Toplevel* window)
{
    should_get_focus.push_back(window);
    // e.g. fullscreens have different layer when active/not-active
    updateStackingOrder();
}


namespace FSP {
    enum Level { None = 0, Low, Medium, High, Extreme };
}

// focus_in -> the window got FocusIn event
// ignore_desktop - call comes from _NET_ACTIVE_WINDOW message, don't refuse just because of window
//     is on a different desktop
bool Workspace::allowClientActivation(Toplevel const* window, xcb_timestamp_t time, bool focus_in, bool ignore_desktop)
{
    // options->focusStealingPreventionLevel :
    // 0 - none    - old KWin behaviour, new windows always get focus
    // 1 - low     - focus stealing prevention is applied normally, when unsure, activation is allowed
    // 2 - normal  - focus stealing prevention is applied normally, when unsure, activation is not allowed,
    //              this is the default
    // 3 - high    - new window gets focus only if it belongs to the active application,
    //              or when no window is currently active
    // 4 - extreme - no window gets focus without user intervention
    if (time == -1U) {
        time = window->userTime();
    }
    auto level = window->control->rules().checkFSP(options->focusStealingPreventionLevel());
    if (sessionManager()->state() == SessionState::Saving && level <= FSP::Medium) { // <= normal
        return true;
    }
    auto ac = mostRecentlyActivatedClient();
    if (focus_in) {
        if (std::find(should_get_focus.cbegin(), should_get_focus.cend(),
                      const_cast<Toplevel*>(window)) != should_get_focus.cend()) {
            // FocusIn was result of KWin's action
            return true;
        }
        // Before getting FocusIn, the active Client already
        // got FocusOut, and therefore got deactivated.
        ac = last_active_client;
    }
    if (time == 0) {   // explicitly asked not to get focus
        if (!window->control->rules().checkAcceptFocus(false))
            return false;
    }
    const int protection = ac ? ac->control->rules().checkFPP(2) : 0;

    // stealing is unconditionally allowed (NETWM behavior)
    if (level == FSP::None || protection == FSP::None)
        return true;

    // The active client "grabs" the focus or stealing is generally forbidden
    if (level == FSP::Extreme || protection == FSP::Extreme)
        return false;

    // Desktop switching is only allowed in the "no protection" case
    if (!ignore_desktop && !window->isOnCurrentDesktop())
        return false; // allow only with level == 0

    // No active client, it's ok to pass focus
    // NOTICE that extreme protection needs to be handled before to allow protection on unmanged windows
    if (ac == nullptr || win::is_desktop(ac)) {
        qCDebug(KWIN_CORE) << "Activation: No client active, allowing";
        return true; // no active client -> always allow
    }

    // TODO window urgency  -> return true?

    // Unconditionally allow intra-client passing around for lower stealing protections
    // unless the active client has High interest
    if (win::belong_to_same_client(window, ac, win::same_client_check::relaxed_for_active)
            && protection < FSP::High) {
        qCDebug(KWIN_CORE) << "Activation: Belongs to active application";
        return true;
    }

    if (!window->isOnCurrentDesktop()) {
        // we allowed explicit self-activation across virtual desktops
        // inside a client or if no client was active, but not otherwise
        return false;
    }

    // High FPS, not intr-client change. Only allow if the active client has only minor interest
    if (level > FSP::Medium && protection > FSP::Low)
        return false;

    if (time == -1U) {  // no time known
        qCDebug(KWIN_CORE) << "Activation: No timestamp at all";
        // Only allow for Low protection unless active client has High interest in focus
        if (level < FSP::Medium && protection < FSP::High)
            return true;
        // no timestamp at all, don't activate - because there's also creation timestamp
        // done on CreateNotify, this case should happen only in case application
        // maps again already used window, i.e. this won't happen after app startup
        return false;
    }

    // Low or medium FSP, usertime comparism is possible
    const xcb_timestamp_t user_time = ac->userTime();
    qCDebug(KWIN_CORE) << "Activation, compared:" << window << ":" << time << ":" << user_time
                 << ":" << (NET::timestampCompare(time, user_time) >= 0);
    return NET::timestampCompare(time, user_time) >= 0;   // time >= user_time
}

// basically the same like allowClientActivation(), this time allowing
// a window to be fully raised upon its own request (XRaiseWindow),
// if refused, it will be raised only on top of windows belonging
// to the same application
bool Workspace::allowFullClientRaising(Toplevel const* window, xcb_timestamp_t time)
{
    auto level = window->control->rules().checkFSP(options->focusStealingPreventionLevel());
    if (sessionManager()->state() == SessionState::Saving && level <= 2) { // <= normal
        return true;
    }
    auto ac = mostRecentlyActivatedClient();
    if (level == 0)   // none
        return true;
    if (level == 4)   // extreme
        return false;
    if (ac == nullptr || win::is_desktop(ac)) {
        qCDebug(KWIN_CORE) << "Raising: No client active, allowing";
        return true; // no active client -> always allow
    }
    // TODO window urgency  -> return true?
    if (win::belong_to_same_client(window, ac, win::same_client_check::relaxed_for_active)) {
        qCDebug(KWIN_CORE) << "Raising: Belongs to active application";
        return true;
    }
    if (level == 3)   // high
        return false;
    xcb_timestamp_t user_time = ac->userTime();
    qCDebug(KWIN_CORE) << "Raising, compared:" << time << ":" << user_time
                 << ":" << (NET::timestampCompare(time, user_time) >= 0);
    return NET::timestampCompare(time, user_time) >= 0;   // time >= user_time
}

// called from Client after FocusIn that wasn't initiated by KWin and the client
// wasn't allowed to activate
void Workspace::restoreFocus()
{
    // this updateXTime() is necessary - as FocusIn events don't have
    // a timestamp *sigh*, kwin's timestamp would be older than the timestamp
    // that was used by whoever caused the focus change, and therefore
    // the attempt to restore the focus would fail due to old timestamp
    updateXTime();
    if (should_get_focus.size() > 0) {
        request_focus(should_get_focus.back());
    } else if (last_active_client) {
        request_focus(last_active_client);
    }
}

void Workspace::clientAttentionChanged(Toplevel* window, bool set)
{
    remove_all(attention_chain, window);
    if (set) {
        attention_chain.push_front(window);
    }
    emit clientDemandsAttentionChanged(window, set);
}

//****************************************
// Group
//****************************************

void Group::startupIdChanged()
{
    KStartupInfoId asn_id;
    KStartupInfoData asn_data;
    bool asn_valid = workspace()->checkStartupNotification(leader_wid, asn_id, asn_data);
    if (!asn_valid)
        return;
    if (asn_id.timestamp() != 0 && user_time != -1U
            && NET::timestampCompare(asn_id.timestamp(), user_time) > 0) {
        user_time = asn_id.timestamp();
    }
}

void Group::updateUserTime(xcb_timestamp_t time)
{
    // copy of win::x11::window::updateUserTime
    if (time == XCB_CURRENT_TIME) {
        updateXTime();
        time = xTime();
    }
    if (time != -1U
            && (user_time == XCB_CURRENT_TIME
                || NET::timestampCompare(time, user_time) > 0))    // time > user_time
        user_time = time;
}

} // namespace
