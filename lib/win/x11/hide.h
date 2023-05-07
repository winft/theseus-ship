/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "actions.h"
#include "hidden_preview.h"
#include "types.h"

#include "base/options.h"
#include "base/x11/xcb/extensions.h"
#include "render/x11/types.h"
#include "win/activation.h"
#include "win/controlling.h"
#include "win/damage.h"
#include "win/scene.h"
#include "win/stacking_order.h"

#include <xcb/xcb_icccm.h>

namespace KWin::win::x11
{

template<typename Win>
bool is_shown(Win const& win)
{
    if (!win.control) {
        return true;
    }
    return !win.control->minimized && !win.hidden;
}

/**
 * Sets the window's mapping state. Possible values are: WithdrawnState, IconicState, NormalState.
 */
template<typename Win>
void export_mapping_state(Win* win, int state)
{
    assert(win->xcb_windows.client != XCB_WINDOW_NONE);
    assert(!win->deleting || state == XCB_ICCCM_WM_STATE_WITHDRAWN);

    auto& atoms = win->space.atoms;

    if (state == XCB_ICCCM_WM_STATE_WITHDRAWN) {
        win->xcb_windows.client.delete_property(atoms->wm_state);
        return;
    }

    assert(state == XCB_ICCCM_WM_STATE_NORMAL || state == XCB_ICCCM_WM_STATE_ICONIC);

    int32_t data[2];
    data[0] = state;
    data[1] = XCB_NONE;
    win->xcb_windows.client.change_property(atoms->wm_state, atoms->wm_state, 32, 2, data);
}

template<typename Win>
void map(Win* win)
{
    // XComposite invalidates backing pixmaps on unmap (minimize, different
    // virtual desktop, etc.).  We kept the last known good pixmap around
    // for use in effects, but now we want to have access to the new pixmap
    if (win->space.base.render->compositor->scene) {
        discard_buffer(*win);
    }

    win->xcb_windows.outer.map();
    win->xcb_windows.wrapper.map();
    win->xcb_windows.client.map();
    win->xcb_windows.input.map();

    export_mapping_state(win, XCB_ICCCM_WM_STATE_NORMAL);
    add_layer_repaint(*win, visible_rect(win));
}

template<typename Win>
void unmap(Win* win)
{
    // Here it may look like a race condition, as some other client might try to unmap
    // the window between these two XSelectInput() calls. However, they're supposed to
    // use XWithdrawWindow(), which also sends a synthetic event to the root window,
    // which won't be missed, so this shouldn't be a problem. The chance the real UnmapNotify
    // will be missed is also very minimal, so I don't think it's needed to grab the server
    // here.

    // Avoid getting UnmapNotify
    win->xcb_windows.wrapper.select_input(client_win_mask);
    win->xcb_windows.outer.unmap();
    win->xcb_windows.wrapper.unmap();
    win->xcb_windows.client.unmap();
    win->xcb_windows.input.unmap();
    win->xcb_windows.wrapper.select_input(client_win_mask | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY);
    export_mapping_state(win, XCB_ICCCM_WM_STATE_ICONIC);
}

template<typename Win>
void internal_show(Win* win)
{
    if (win->mapping == mapping_state::mapped) {
        return;
    }

    auto old = win->mapping;
    win->mapping = mapping_state::mapped;

    if (old == mapping_state::unmapped || old == mapping_state::withdrawn) {
        map(win);
    }

    if (old == mapping_state::kept) {
        win->xcb_windows.input.map();
        update_hidden_preview(win);
    }

    Q_EMIT win->qobject->windowShown();
}

template<typename Win>
void internal_hide(Win* win)
{
    if (win->mapping == mapping_state::unmapped) {
        return;
    }

    auto old = win->mapping;
    win->mapping = mapping_state::unmapped;

    if (old == mapping_state::mapped || old == mapping_state::kept) {
        unmap(win);
    }
    if (old == mapping_state::kept) {
        update_hidden_preview(win);
    }

    win->space.base.render->compositor->addRepaint(visible_rect(win));
    process_window_hidden(win->space, *win);
    Q_EMIT win->qobject->windowHidden();
}

template<typename Win>
void internal_keep(Win* win)
{
    assert(win->space.base.render->compositor->scene);

    if (win->mapping == mapping_state::kept) {
        return;
    }

    auto old = win->mapping;
    win->mapping = mapping_state::kept;

    if (old == mapping_state::unmapped || old == mapping_state::withdrawn) {
        map(win);
    }

    win->xcb_windows.input.unmap();
    if (win->control->active) {
        // get rid of input focus, bug #317484
        focus_to_null(win->space);
    }

    update_hidden_preview(win);
    win->space.base.render->compositor->addRepaint(visible_rect(win));
    process_window_hidden(win->space, *win);
}

template<typename Win>
void update_visibility(Win* win)
{
    assert(win->control);

    if (win->deleting) {
        return;
    }

    if (win->hidden) {
        win->net_info->setState(net::Hidden, net::Hidden);
        win::set_skip_taskbar(win, true);
        if (win->space.base.render->compositor->scene
            && win->space.base.options->qobject->hiddenPreviews()
                == render::x11::hidden_preview::always) {
            internal_keep(win);
        } else {
            internal_hide(win);
        }
        return;
    }

    win::set_skip_taskbar(win, win->control->original_skip_taskbar);

    if (win->control->minimized) {
        win->net_info->setState(net::Hidden, net::Hidden);
        if (win->space.base.render->compositor->scene
            && win->space.base.options->qobject->hiddenPreviews()
                == render::x11::hidden_preview::always) {
            internal_keep(win);
        } else {
            internal_hide(win);
        }
        return;
    }

    win->net_info->setState(net::States(), net::Hidden);
    if (!on_current_desktop(win)) {
        if (win->space.base.render->compositor->scene
            && win->space.base.options->qobject->hiddenPreviews()
                != render::x11::hidden_preview::never) {
            internal_keep(win);
        } else {
            internal_hide(win);
        }
        return;
    }
    internal_show(win);
}

template<typename Win>
void show_on_screen_edge(Win& win)
{
    QObject::disconnect(win.notifiers.edge_remove);

    win.hideClient(false);
    win::set_keep_below(&win, false);
    xcb_delete_property(win.space.base.x11_data.connection,
                        win.xcb_windows.client,
                        win.space.atoms->kde_screen_edge_show);
}

template<typename Win>
void do_minimize(Win& win)
{
    update_visibility(&win);
    update_allowed_actions(&win);
    propagate_minimized_to_transients(win);
}

template<typename Win>
void hide_window(Win& win, bool hide)
{
    if (win.hidden == hide) {
        return;
    }
    win.hidden = hide;
    update_visibility(&win);
}

}
