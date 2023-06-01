/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/types.h"

#include <xcb/sync.h>

namespace KWin::win::x11
{

/**
 * @brief Defines predicate matches on how to search for a window.
 */
enum class predicate_match {
    window,
    wrapper_id,
    frame_id,
    input_id,
};

enum class mapping_state {
    withdrawn, ///< Not handled, as per ICCCM WithdrawnState
    mapped,    ///< The frame is mapped
    unmapped,  ///< The frame is not mapped
    kept,      ///< The frame should be unmapped, but is kept (For compositing)
};

constexpr long client_win_mask = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE
    | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_KEYMAP_STATE
    | XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION | // need this, too!
    XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE
    | XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
    | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;

// Window types with control.
const window_type_mask supported_managed_window_types_mask = window_type_mask::normal
    | window_type_mask::desktop | window_type_mask::dock | window_type_mask::toolbar
    | window_type_mask::menu
    | window_type_mask::dialog /*| window_type_mask::override*/ | window_type_mask::top_menu
    | window_type_mask::utility | window_type_mask::splash | window_type_mask::notification
    | window_type_mask::on_screen_display | window_type_mask::critical_notification
    | window_type_mask::applet_popup;

}
