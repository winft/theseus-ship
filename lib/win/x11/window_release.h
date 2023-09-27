/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "client.h"
#include "meta.h"
#include "transient.h"

#include "base/x11/grabs.h"
#include "base/x11/xcb/extensions.h"
#include "base/x11/xcb/helpers.h"
#include "utils/blocker.h"
#include "win/input.h"
#include "win/rules.h"
#include "win/shortcut_set.h"
#include "win/space_areas_helpers.h"
#include "win/tabbox.h"
#include "win/window_release.h"

#include <csignal>
#include <xcb/xcb_icccm.h>

namespace KWin::win::x11
{

template<typename Win>
QWindow* find_internal_window(Win const& win)
{
    auto const windows = qApp->topLevelWindows();
    for (auto xcb_win : windows) {
        if (xcb_win->handle() && xcb_win->winId() == win.xcb_windows.client) {
            return xcb_win;
        }
    }
    return nullptr;
}

// before being deleted, remove references to everything that's now owner by the remnant
template<typename Win>
void disown_data_passed_to_remnant(Win& win)
{
    win.client_machine = nullptr;
    win.net_info = nullptr;
}

template<typename Space, typename Win>
void remove_controlled_window_from_space(Space& space, Win* win)
{
    using var_win = typename Space::window_t;

    if (var_win(win) == space.active_popup_client) {
        close_active_popup(space);
    }

    if (space.user_actions_menu->isMenuClient(win)) {
        space.user_actions_menu->close();
    }

    if (space.client_keys_client == var_win(win)) {
        shortcut_dialog_done(space, false);
    }
    if (!win->control->shortcut.isEmpty()) {
        // Remove from client_keys.
        set_shortcut(win, QString());

        // Needed, since this is otherwise delayed by setShortcut() and wouldn't run
        window_shortcut_updated(space, win);
    }

    assert(contains(space.windows, var_win(win)));

    // TODO: if marked client is removed, notify the marked list
    remove_window_from_lists(space, win);
    remove_all(space.stacking.attention_chain, var_win(win));

    auto group = find_group(space, win->xcb_windows.client);
    if (group) {
        group->lostLeader();
    }

    if (var_win(win) == space.stacking.most_recently_raised) {
        space.stacking.most_recently_raised = {};
    }

    remove_all(space.stacking.should_get_focus, var_win(win));

    assert(var_win(win) != space.stacking.active);

    if (var_win(win) == space.stacking.last_active) {
        space.stacking.last_active = {};
    }
    if (var_win(win) == space.stacking.delayfocus_window) {
        cancel_delay_focus(space);
    }

    Q_EMIT space.qobject->clientRemoved(win->meta.signal_id);

    space.stacking.order.update_count();
    update_space_areas(space);
    update_tabbox(space);
}

template<typename Win>
void destroy_damage_handle(Win& win)
{
    if (win.damage.handle == XCB_NONE) {
        return;
    }
    xcb_damage_destroy(win.space.base.x11_data.connection, win.damage.handle);
    win.damage.handle = XCB_NONE;
}

template<typename Win>
void reset_have_resize_effect(Win& win)
{
    if (win.control) {
        win.control->have_resize_effect = false;
    }
}

template<typename Win>
void finish_unmanaged_removal(Win* win, Win* remnant)
{
    using var_win = typename Win::space_t::window_t;

    auto& space = win->space;
    assert(contains(space.windows, var_win(win)));

    remove_window_from_lists(space, win);
    space.base.render->compositor->addRepaint(visible_rect(win));

    Q_EMIT space.qobject->unmanagedRemoved(win->meta.signal_id);

    if (remnant) {
        disown_data_passed_to_remnant(*win);
        remnant->remnant->unref();
        delete win;
    } else {
        delete_window_from_space(space, *win);
    }
}

template<typename Win>
Win* create_remnant_window(Win& source)
{
    auto win = win::create_remnant_window(source);
    if (!win) {
        return {};
    }

    transfer_remnant_data(source, *win);

    assert(win->damage.handle == XCB_NONE);

    win->net_info = source.net_info;

    if (auto winfo = dynamic_cast<x11::win_info<Win>*>(win->net_info)) {
        winfo->disable();
    }

    win->xcb_windows.client.reset(
        source.space.base.x11_data.connection, source.xcb_windows.client, false);
    win->xcb_visual = source.xcb_visual;
    win->is_shape = source.is_shape;
    win->client_machine = source.client_machine;
    win->m_wmClientLeader = get_wm_client_leader(source);

    space_add_remnant(source, *win);
    scene_add_remnant(*win);
    return win;
}

template<typename Win>
void release_unmanaged(Win* win, bool on_shutdown)
{
    Win* del = nullptr;
    if (!on_shutdown) {
        del = x11::create_remnant_window<Win>(*win);
    }
    Q_EMIT win->qobject->closed();

    // Don't affect our own windows.
    if (!find_internal_window(*win)) {
        if (base::x11::xcb::extensions::self()->is_shape_available()) {
            xcb_shape_select_input(
                win->space.base.x11_data.connection, win->xcb_windows.client, false);
        }
        base::x11::xcb::select_input(
            win->space.base.x11_data.connection, win->xcb_windows.client, XCB_EVENT_MASK_NO_EVENT);
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
    auto del = x11::create_remnant_window<Win>(*win);
    Q_EMIT win->qobject->closed();
    finish_unmanaged_removal(win, del);
}

template<typename Win>
void release_window(Win* win, bool on_shutdown)
{
    using var_win = typename Win::space_t::window_t;
    auto& space = win->space;

    Q_ASSERT(!win->deleting);
    win->deleting = true;

    if (!win->control) {
        destroy_damage_handle(*win);
        release_unmanaged(win, on_shutdown);
        return;
    }

#if KWIN_BUILD_TABBOX
    auto& tabbox = space.tabbox;
    if (tabbox->is_displayed() && tabbox->current_client()
        && tabbox->current_client() == var_win(win)) {
        tabbox->next_prev(true);
    }
#endif

    win->control->destroy_plasma_wayland_integration();
    destroy_damage_handle(*win);
    reset_have_resize_effect(*win);

    Win* del = nullptr;
    if (on_shutdown) {
        // Move the client window to maintain its position.
        auto const offset = QPoint(left_border(win), top_border(win));
        win->setFrameGeometry(win->geo.frame.translated(offset));
    } else {
        del = x11::create_remnant_window<Win>(*win);
    }

    if (win->control->move_resize.enabled) {
        Q_EMIT win->qobject->clientFinishUserMovedResized();
    }

    Q_EMIT win->qobject->closed();

    // Remove ForceTemporarily rules
    rules::discard_used_rules(*space.rule_book, *win, true);

    blocker block(space.stacking.order);

    if (win->control->move_resize.enabled) {
        win->leaveMoveResize();
    }

    finish_rules(win);
    win->geo.update.block++;

    if (on_current_desktop(*win) && win->isShown()) {
        space.base.render->compositor->addRepaint(visible_rect(win));
    }

    // Grab X during the release to make removing of properties, setting to withdrawn state
    // and repareting to root an atomic operation
    // (https://lists.kde.org/?l=kde-devel&m=116448102901184&w=2)
    base::x11::grab_server(space.base.x11_data.connection);
    export_mapping_state(win, XCB_ICCCM_WM_STATE_WITHDRAWN);

    // So that it's not considered visible anymore (can't use hideClient(), it would set flags)
    win->hidden = true;

    if (!on_shutdown) {
        process_window_hidden(space, *win);
    }

    // Destroying decoration would cause ugly visual effect
    win->xcb_windows.outer.unmap();

    win->control->destroy_decoration();
    clean_grouping(win);

    if (!on_shutdown) {
        remove_controlled_window_from_space(space, win);
        // Only when the window is being unmapped, not when closing down KWin (NETWM
        // sections 5.5,5.7)
        win->net_info->setDesktop(0);

        // Reset all state flags
        win->net_info->setState(net::States(), win->net_info->state());
    }

    auto& atoms = space.atoms;
    win->xcb_windows.client.delete_property(atoms->kde_net_wm_user_creation_time);
    win->xcb_windows.client.delete_property(atoms->net_frame_extents);
    win->xcb_windows.client.delete_property(atoms->kde_net_wm_frame_strut);

    auto const client_rect = frame_to_client_rect(win, win->geo.frame);
    win->xcb_windows.client.reparent(
        space.base.x11_data.root_window, client_rect.x(), client_rect.y());

    xcb_change_save_set(
        space.base.x11_data.connection, XCB_SET_MODE_DELETE, win->xcb_windows.client);
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

    win->xcb_windows.wrapper.reset();
    win->xcb_windows.outer.reset();

    // Don't use GeometryUpdatesBlocker, it would now set the geometry
    win->geo.update.block--;

    if (del) {
        disown_data_passed_to_remnant(*win);
        del->remnant->unref();
        delete win;
    } else {
        delete_window_from_space(space, *win);
    }

    base::x11::ungrab_server(space.base.x11_data.connection);
}

/**
 * Like release(), but window is already destroyed (for example app closed it).
 */
template<typename Win>
void destroy_window(Win* win)
{
    using var_win = typename Win::space_t::window_t;

    assert(!win->deleting);
    win->deleting = true;

    if (!win->control) {
        destroy_unmanaged(win);
        return;
    }

#if KWIN_BUILD_TABBOX
    auto& tabbox = win->space.tabbox;
    if (tabbox && tabbox->is_displayed() && tabbox->current_client() == var_win(win)) {
        tabbox->next_prev(true);
    }
#endif

    win->control->destroy_plasma_wayland_integration();
    reset_have_resize_effect(*win);

    auto del = x11::create_remnant_window<Win>(*win);

    if (win->control->move_resize.enabled) {
        Q_EMIT win->qobject->clientFinishUserMovedResized();
    }

    Q_EMIT win->qobject->closed();

    // Remove ForceTemporarily rules
    rules::discard_used_rules(*win->space.rule_book, *win, true);

    blocker block(win->space.stacking.order);
    if (win->control->move_resize.enabled) {
        win->leaveMoveResize();
    }

    finish_rules(win);
    win->geo.update.block++;

    if (on_current_desktop(*win) && win->isShown()) {
        win->space.base.render->compositor->addRepaint(visible_rect(win));
    }

    // So that it's not considered visible anymore
    win->hidden = true;

    process_window_hidden(win->space, *win);
    win->control->destroy_decoration();
    clean_grouping(win);
    remove_controlled_window_from_space(win->space, win);

    // invalidate
    win->xcb_windows.wrapper.reset();
    win->xcb_windows.outer.reset();

    // Don't use GeometryUpdatesBlocker, it would now set the geometry
    win->geo.update.block--;

    if (del) {
        disown_data_passed_to_remnant(*win);
        del->remnant->unref();
        delete win;
    } else {
        delete_window_from_space(win->space, *win);
    }
}

template<typename Win>
void cleanup_window(Win& win)
{
    if (win.kill_helper_pid && !::kill(win.kill_helper_pid, 0)) {
        // The process is still alive.
        ::kill(win.kill_helper_pid, SIGTERM);
        win.kill_helper_pid = 0;
    }

    if (win.sync_request.alarm != XCB_NONE) {
        xcb_sync_destroy_alarm(win.space.base.x11_data.connection, win.sync_request.alarm);
    }

    assert(!win.control || !win.control->move_resize.enabled);
    assert(win.xcb_windows.client != XCB_WINDOW_NONE);
    assert(win.xcb_windows.wrapper == XCB_WINDOW_NONE);
    assert(win.xcb_windows.outer == XCB_WINDOW_NONE);

    delete win.client_machine;
    delete win.net_info;
    win.space.windows_map.erase(win.meta.signal_id);
}

/// Kills the window via XKill
template<typename Win>
void handle_kill_window(Win& win)
{
    qCDebug(KWIN_CORE) << "x11::kill_window:" << caption(&win);
    kill_process(&win, false);

    // Always kill this client at the server
    win.xcb_windows.client.kill();

    x11::destroy_window(&win);
}

template<typename Win>
bool is_closeable(Win const& win)
{
    return win.control->rules.checkCloseable(win.motif_hints.close() && !is_special_window(&win));
}

template<typename Win>
void close_window(Win& win)
{
    if (!win.isCloseable()) {
        return;
    }

    // Update user time, because the window may create a confirming dialog.
    update_user_time(&win);

    if (win.net_info->supportsProtocol(net::DeleteWindowProtocol)) {
        send_client_message(win.space.base.x11_data,
                            win.xcb_windows.client,
                            win.space.atoms->wm_protocols,
                            win.space.atoms->wm_delete_window);
        ping(&win);
    } else {
        // Client will not react on wm_delete_window. We have not choice
        // but destroy his connection to the XServer.
        win.killWindow();
    }
}

}
