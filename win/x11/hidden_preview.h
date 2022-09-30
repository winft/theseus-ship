/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

#include "base/x11/xcb/extensions.h"

namespace KWin::win::x11
{

template<typename Win>
bool hidden_preview(Win* win)
{
    return win->mapping == mapping_state::kept;
}

template<typename Win>
void update_input_shape(Win& win);

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
        win->space.stacking.order.force_restacking();
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
        win->space.stacking.order.force_restacking();
        update_input_shape(*win);
    }
}

}
