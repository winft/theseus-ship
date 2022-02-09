/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "client.h"
#include "input.h"
#include "window.h"

#include "win/controlling.h"
#include "win/scene.h"
#include "win/stacking_order.h"

#include <xcb/xcb_icccm.h>

namespace KWin::win::x11
{

template<typename Win>
void map(Win* win)
{
    // XComposite invalidates backing pixmaps on unmap (minimize, different
    // virtual desktop, etc.).  We kept the last known good pixmap around
    // for use in effects, but now we want to have access to the new pixmap
    if (win::compositing()) {
        win->discardWindowPixmap();
    }

    win->xcb_windows.outer.map();
    win->xcb_windows.wrapper.map();
    win->xcb_windows.client.map();
    win->xcb_windows.input.map();

    export_mapping_state(win, XCB_ICCCM_WM_STATE_NORMAL);
    win->addLayerRepaint(win::visible_rect(win));
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
bool hidden_preview(Win* win)
{
    return win->mapping == mapping_state::kept;
}

/**
 * XComposite doesn't keep window pixmaps of unmapped windows, which means
 * there wouldn't be any previews of windows that are minimized or on another
 * virtual desktop. Therefore rawHide() actually keeps such windows mapped.
 * However special care needs to be taken so that such windows don't interfere.
 * Therefore they're put very low in the stacking order and they have input shape
 * set to none, which hopefully is enough. If there's no input shape available,
 * then it's hoped that there will be some other desktop above it *shrug*.
 * Using normal shape would be better, but that'd affect other things, e.g. painting
 * of the actual preview.
 */
template<typename Win>
void update_hidden_preview(Win* win)
{
    if (hidden_preview(win)) {
        workspace()->stacking_order->force_restacking();
        if (base::x11::xcb::extensions::self()->is_shape_input_available()) {
            xcb_shape_rectangles(connection(),
                                 XCB_SHAPE_SO_SET,
                                 XCB_SHAPE_SK_INPUT,
                                 XCB_CLIP_ORDERING_UNSORTED,
                                 win->frameId(),
                                 0,
                                 0,
                                 0,
                                 nullptr);
        }
    } else {
        workspace()->stacking_order->force_restacking();
        win->update_input_shape();
    }
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

    Q_EMIT win->windowShown(win);
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

    win->addWorkspaceRepaint(win::visible_rect(win));
    workspace()->clientHidden(win);
    Q_EMIT win->windowHidden(win);
}

template<typename Win>
void internal_keep(Win* win)
{
    assert(win::compositing());

    if (win->mapping == mapping_state::kept) {
        return;
    }

    auto old = win->mapping;
    win->mapping = mapping_state::kept;

    if (old == mapping_state::unmapped || old == mapping_state::withdrawn) {
        map(win);
    }

    win->xcb_windows.input.unmap();
    if (win->control->active()) {
        // get rid of input focus, bug #317484
        workspace()->focusToNull();
    }

    update_hidden_preview(win);
    win->addWorkspaceRepaint(win::visible_rect(win));
    workspace()->clientHidden(win);
}

template<typename Win>
void update_visibility(Win* win)
{
    if (win->deleting) {
        return;
    }

    if (win->hidden) {
        win->info->setState(NET::Hidden, NET::Hidden);
        win::set_skip_taskbar(win, true);
        if (win::compositing() && options->hiddenPreviews() == HiddenPreviewsAlways) {
            internal_keep(win);
        } else {
            internal_hide(win);
        }
        return;
    }

    win::set_skip_taskbar(win, win->control->original_skip_taskbar());

    if (win->control->minimized()) {
        win->info->setState(NET::Hidden, NET::Hidden);
        if (win::compositing() && options->hiddenPreviews() == HiddenPreviewsAlways) {
            internal_keep(win);
        } else {
            internal_hide(win);
        }
        return;
    }

    win->info->setState(NET::States(), NET::Hidden);
    if (!win->isOnCurrentDesktop()) {
        if (win::compositing() && options->hiddenPreviews() != HiddenPreviewsNever) {
            internal_keep(win);
        } else {
            internal_hide(win);
        }
        return;
    }
    internal_show(win);
}

}
