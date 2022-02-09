/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "toplevel.h"
#include "utils.h"
#include "win/rules.h"

#ifdef KWIN_BUILD_TABBOX
#include "tabbox.h"
#endif

namespace KWin::win::x11
{

template<typename Win>
void release_unmanaged(Win* win, ReleaseReason releaseReason = ReleaseReason::Release)
{
    Toplevel* del = nullptr;
    if (releaseReason != ReleaseReason::KWinShutsDown) {
        del = Toplevel::create_remnant(win);
    }
    Q_EMIT win->windowClosed(win, del);
    win->finishCompositing(releaseReason);

    // Don't affect our own windows.
    if (!QWidget::find(win->xcb_window()) && releaseReason != ReleaseReason::Destroyed) {
        if (base::x11::xcb::extensions::self()->is_shape_available()) {
            xcb_shape_select_input(connection(), win->xcb_window(), false);
        }
        base::x11::xcb::select_input(win->xcb_window(), XCB_EVENT_MASK_NO_EVENT);
    }

    if (releaseReason != ReleaseReason::KWinShutsDown) {
        workspace()->removeUnmanaged(win);
        win->addWorkspaceRepaint(win::visible_rect(del));
        win->disownDataPassedToDeleted();
        del->remnant()->unref();
    }
    delete win;
}

template<typename Win>
void release_window(Win* win, bool on_shutdown)
{
    Q_ASSERT(!win->deleting);
    win->deleting = true;

    if (!win->control) {
        release_unmanaged(win, on_shutdown ? ReleaseReason::KWinShutsDown : ReleaseReason::Release);
        return;
    }

#ifdef KWIN_BUILD_TABBOX
    auto tabbox = TabBox::TabBox::self();
    if (tabbox->isDisplayed() && tabbox->currentClient() == win) {
        tabbox->nextPrev(true);
    }
#endif

    win->control->destroy_wayland_management();

    Toplevel* del = nullptr;
    if (on_shutdown) {
        // Move the client window to maintain its position.
        auto const offset = QPoint(left_border(win), top_border(win));
        win->setFrameGeometry(win->frameGeometry().translated(offset));
    } else {
        del = win->create_remnant(win);
    }

    if (win->control->move_resize().enabled) {
        Q_EMIT win->clientFinishUserMovedResized(win);
    }

    Q_EMIT win->windowClosed(win, del);
    win->finishCompositing();

    // Remove ForceTemporarily rules
    RuleBook::self()->discardUsed(win, true);

    Blocker blocker(workspace()->stacking_order);

    if (win->control->move_resize().enabled) {
        win->leaveMoveResize();
    }

    finish_rules(win);
    win->geometry_update.block++;

    if (win->isOnCurrentDesktop() && win->isShown()) {
        win->addWorkspaceRepaint(visible_rect(win));
    }

    // Grab X during the release to make removing of properties, setting to withdrawn state
    // and repareting to root an atomic operation
    // (https://lists.kde.org/?l=kde-devel&m=116448102901184&w=2)
    grabXServer();
    export_mapping_state(win, XCB_ICCCM_WM_STATE_WITHDRAWN);

    // So that it's not considered visible anymore (can't use hideClient(), it would set flags)
    win->hidden = true;

    if (!on_shutdown) {
        workspace()->clientHidden(win);
    }

    // Destroying decoration would cause ugly visual effect
    win->xcb_windows.outer.unmap();

    win->control->destroy_decoration();
    clean_grouping(win);

    if (!on_shutdown) {
        workspace()->removeClient(win);
        // Only when the window is being unmapped, not when closing down KWin (NETWM
        // sections 5.5,5.7)
        win->info->setDesktop(0);

        // Reset all state flags
        win->info->setState(NET::States(), win->info->state());
    }

    auto& atoms = win->space.atoms;
    win->xcb_windows.client.delete_property(atoms->kde_net_wm_user_creation_time);
    win->xcb_windows.client.delete_property(atoms->net_frame_extents);
    win->xcb_windows.client.delete_property(atoms->kde_net_wm_frame_strut);

    auto const client_rect = frame_to_client_rect(win, win->frameGeometry());
    win->xcb_windows.client.reparent(rootWindow(), client_rect.x(), client_rect.y());

    xcb_change_save_set(connection(), XCB_SET_MODE_DELETE, win->xcb_windows.client);
    win->xcb_windows.client.select_input(XCB_EVENT_MASK_NO_EVENT);

    if (on_shutdown) {
        // Map the window, so it can be found after another WM is started
        win->xcb_windows.client.map();
        // TODO: Preserve minimized etc. state?
    } else {
        // Make sure it's not mapped if the app unmapped it (#65279). The app
        // may do map+unmap before we initially map the window by calling rawShow() from manage().
        win->xcb_windows.client.unmap();
    }

    win->xcb_windows.client.reset();
    win->xcb_windows.wrapper.reset();
    win->xcb_windows.outer.reset();

    // Don't use GeometryUpdatesBlocker, it would now set the geometry
    win->geometry_update.block--;

    if (!on_shutdown) {
        win->disownDataPassedToDeleted();
        del->remnant()->unref();
    }

    delete win;
    ungrabXServer();
}

/**
 * Like release(), but window is already destroyed (for example app closed it).
 */
template<typename Win>
void destroy_window(Win* win)
{
    assert(!win->deleting);
    win->deleting = true;

    if (!win->control) {
        release_unmanaged(win, ReleaseReason::Destroyed);
        return;
    }

#ifdef KWIN_BUILD_TABBOX
    auto tabbox = TabBox::TabBox::self();
    if (tabbox && tabbox->isDisplayed() && tabbox->currentClient() == win) {
        tabbox->nextPrev(true);
    }
#endif

    win->control->destroy_wayland_management();

    auto del = win->create_remnant(win);

    if (win->control->move_resize().enabled) {
        Q_EMIT win->clientFinishUserMovedResized(win);
    }
    Q_EMIT win->windowClosed(win, del);

    win->finishCompositing(ReleaseReason::Destroyed);

    // Remove ForceTemporarily rules
    RuleBook::self()->discardUsed(win, true);

    Blocker blocker(workspace()->stacking_order);
    if (win->control->move_resize().enabled) {
        win->leaveMoveResize();
    }

    finish_rules(win);
    win->geometry_update.block++;

    if (win->isOnCurrentDesktop() && win->isShown()) {
        win->addWorkspaceRepaint(visible_rect(win));
    }

    // So that it's not considered visible anymore
    win->hidden = true;

    workspace()->clientHidden(win);
    win->control->destroy_decoration();
    clean_grouping(win);
    workspace()->removeClient(win);

    // invalidate
    win->xcb_windows.client.reset();
    win->xcb_windows.wrapper.reset();
    win->xcb_windows.outer.reset();

    // Don't use GeometryUpdatesBlocker, it would now set the geometry
    win->geometry_update.block--;
    win->disownDataPassedToDeleted();
    del->remnant()->unref();
    delete win;
}

}
