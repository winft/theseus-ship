/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/grabs.h"
#include "toplevel.h"
#include "utils/blocker.h"
#include "win/rules.h"
#include "win/space_helpers.h"
#include "win/window_release.h"

#if KWIN_BUILD_TABBOX
#include "win/tabbox/tabbox.h"
#endif

namespace KWin::win::x11
{

template<typename Space, typename Win>
void remove_controlled_window_from_space(Space& space, Win* win)
{
    if (win == space.active_popup_client) {
        space.closeActivePopup();
    }

    if (space.user_actions_menu->isMenuClient(win)) {
        space.user_actions_menu->close();
    }

    if (space.client_keys_client == win) {
        space.setupWindowShortcutDone(false);
    }
    if (!win->control->shortcut().isEmpty()) {
        // Remove from client_keys.
        set_shortcut(win, QString());

        // Needed, since this is otherwise delayed by setShortcut() and wouldn't run
        space.clientShortcutUpdated(win);
    }

    assert(contains(space.m_windows, win));

    // TODO: if marked client is removed, notify the marked list
    remove_window_from_lists(space, win);
    remove_all(space.attention_chain, win);

    auto group = space.findGroup(win->xcb_window);
    if (group) {
        group->lostLeader();
    }

    if (win == space.most_recently_raised) {
        space.most_recently_raised = nullptr;
    }

    remove_all(space.should_get_focus, win);

    assert(win != space.active_client);

    if (win == space.last_active_client) {
        space.last_active_client = nullptr;
    }
    if (win == space.delayfocus_client)
        space.cancelDelayFocus();

    Q_EMIT space.clientRemoved(win);

    space.stacking_order->update(true);
    space.updateClientArea();
    space.updateTabbox();
}

template<typename Win>
void destroy_damage_handle(Win& win)
{
    if (win.damage_handle == XCB_NONE) {
        return;
    }
    xcb_damage_destroy(connection(), win.damage_handle);
    win.damage_handle = XCB_NONE;
}

template<typename Win>
void reset_have_resize_effect(Win& win)
{
    if (win.control) {
        win.control->reset_have_resize_effect();
    }
}

template<typename Win>
void finish_unmanaged_removal(Win* win, Toplevel* remnant)
{
    auto& space = win->space;
    assert(contains(space.m_windows, win));

    remove_window_from_lists(space, win);
    space.render.addRepaint(visible_rect(win));

    Q_EMIT space.unmanagedRemoved(win);

    if (remnant) {
        win->disownDataPassedToDeleted();
        remnant->remnant->unref();
        delete win;
    } else {
        delete_window_from_space(space, win);
    }
}

template<typename Win>
void release_unmanaged(Win* win, bool on_shutdown)
{
    Toplevel* del = nullptr;
    if (!on_shutdown) {
        del = create_remnant<Win>(*win);
    }
    Q_EMIT win->closed(win);

    // Don't affect our own windows.
    if (!QWidget::find(win->xcb_window)) {
        if (base::x11::xcb::extensions::self()->is_shape_available()) {
            xcb_shape_select_input(connection(), win->xcb_window, false);
        }
        base::x11::xcb::select_input(win->xcb_window, XCB_EVENT_MASK_NO_EVENT);
    }

    if (on_shutdown) {
        delete win;
    } else {
        finish_unmanaged_removal(win, del);
    }
}

template<typename Win>
void destroy_unmanaged(Win* win)
{
    auto del = create_remnant<Win>(*win);
    Q_EMIT win->closed(win);
    finish_unmanaged_removal(win, del);
}

template<typename Win>
void release_window(Win* win, bool on_shutdown)
{
    Q_ASSERT(!win->deleting);
    win->deleting = true;

    if (!win->control) {
        destroy_damage_handle(*win);
        release_unmanaged(win, on_shutdown);
        return;
    }

#if KWIN_BUILD_TABBOX
    auto& tabbox = win->space.tabbox;
    if (tabbox->is_displayed() && tabbox->current_client() == win) {
        tabbox->next_prev(true);
    }
#endif

    win->control->destroy_wayland_management();
    destroy_damage_handle(*win);
    reset_have_resize_effect(*win);

    Toplevel* del = nullptr;
    if (on_shutdown) {
        // Move the client window to maintain its position.
        auto const offset = QPoint(left_border(win), top_border(win));
        win->setFrameGeometry(win->frameGeometry().translated(offset));
    } else {
        del = create_remnant<Win>(*win);
    }

    if (win->control->move_resize().enabled) {
        Q_EMIT win->clientFinishUserMovedResized(win);
    }

    Q_EMIT win->closed(win);

    // Remove ForceTemporarily rules
    win->space.rule_book->discardUsed(win, true);

    blocker block(win->space.stacking_order);

    if (win->control->move_resize().enabled) {
        win->leaveMoveResize();
    }

    finish_rules(win);
    win->geometry_update.block++;

    if (win->isOnCurrentDesktop() && win->isShown()) {
        win->space.render.addRepaint(visible_rect(win));
    }

    // Grab X during the release to make removing of properties, setting to withdrawn state
    // and repareting to root an atomic operation
    // (https://lists.kde.org/?l=kde-devel&m=116448102901184&w=2)
    base::x11::grab_server();
    export_mapping_state(win, XCB_ICCCM_WM_STATE_WITHDRAWN);

    // So that it's not considered visible anymore (can't use hideClient(), it would set flags)
    win->hidden = true;

    if (!on_shutdown) {
        win->space.clientHidden(win);
    }

    // Destroying decoration would cause ugly visual effect
    win->xcb_windows.outer.unmap();

    win->control->destroy_decoration();
    clean_grouping(win);

    if (!on_shutdown) {
        remove_controlled_window_from_space(win->space, win);
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

    if (del) {
        win->disownDataPassedToDeleted();
        del->remnant->unref();
        delete win;
    } else {
        delete_window_from_space(win->space, win);
    }

    base::x11::ungrab_server();
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
        destroy_unmanaged(win);
        return;
    }

#if KWIN_BUILD_TABBOX
    auto& tabbox = win->space.tabbox;
    if (tabbox && tabbox->is_displayed() && tabbox->current_client() == win) {
        tabbox->next_prev(true);
    }
#endif

    win->control->destroy_wayland_management();
    reset_have_resize_effect(*win);

    auto del = create_remnant<Win>(*win);

    if (win->control->move_resize().enabled) {
        Q_EMIT win->clientFinishUserMovedResized(win);
    }

    Q_EMIT win->closed(win);

    // Remove ForceTemporarily rules
    win->space.rule_book->discardUsed(win, true);

    blocker block(win->space.stacking_order);
    if (win->control->move_resize().enabled) {
        win->leaveMoveResize();
    }

    finish_rules(win);
    win->geometry_update.block++;

    if (win->isOnCurrentDesktop() && win->isShown()) {
        win->space.render.addRepaint(visible_rect(win));
    }

    // So that it's not considered visible anymore
    win->hidden = true;

    win->space.clientHidden(win);
    win->control->destroy_decoration();
    clean_grouping(win);
    remove_controlled_window_from_space(win->space, win);

    // invalidate
    win->xcb_windows.client.reset();
    win->xcb_windows.wrapper.reset();
    win->xcb_windows.outer.reset();

    // Don't use GeometryUpdatesBlocker, it would now set the geometry
    win->geometry_update.block--;

    if (del) {
        win->disownDataPassedToDeleted();
        del->remnant->unref();
        delete win;
    } else {
        delete_window_from_space(win->space, win);
    }
}

}
