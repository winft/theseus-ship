/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/x11/event_filter.h"
#include "input/logging.h"
#include "input/xkb/keymap.h"

#define explicit cpp_explicit_compat
#include <xcb/xkb.h>
#undef explicit
#include <xkbcommon/xkbcommon-x11.h>

namespace KWin::input::x11
{

template<typename Keyboard>
bool xkb_handle_event(Keyboard& keyboard, xcb_generic_event_t* gen_event);

template<typename Keyboard>
class xkb_filter : public base::x11::event_filter
{
public:
    xkb_filter(uint32_t type, Keyboard& keyboard, base::x11::event_filter_manager& manager)
        : base::x11::event_filter(manager, type)
        , keyboard{keyboard}
    {
    }

    ~xkb_filter() override = default;

    bool event(xcb_generic_event_t* event) override
    {
        return xkb_handle_event(keyboard, event);
    }

    Keyboard& keyboard;
};

inline int32_t xkb_get_device_id(xcb_connection_t* connection)
{
    auto device_id = xkb_x11_get_core_keyboard_device_id(connection);
    if (device_id == -1) {
        throw std::runtime_error("xkb_x11_get_core_keyboard_device_id failed");
    }
    return device_id;
}

inline bool xkb_select_events(xcb_connection_t* connection, uint32_t device_id)
{
    auto selected_events = (XCB_XKB_EVENT_TYPE_NEW_KEYBOARD_NOTIFY | XCB_XKB_EVENT_TYPE_MAP_NOTIFY
                            | XCB_XKB_EVENT_TYPE_STATE_NOTIFY);

    auto new_keyboard_details = static_cast<uint16_t>(XCB_XKB_NKN_DETAIL_KEYCODES);

    auto affect_map = static_cast<uint16_t>(
        XCB_XKB_MAP_PART_KEY_TYPES | XCB_XKB_MAP_PART_KEY_SYMS | XCB_XKB_MAP_PART_MODIFIER_MAP
        | XCB_XKB_MAP_PART_EXPLICIT_COMPONENTS | XCB_XKB_MAP_PART_KEY_ACTIONS
        | XCB_XKB_MAP_PART_VIRTUAL_MODS | XCB_XKB_MAP_PART_VIRTUAL_MOD_MAP);

    auto state_parts
        = static_cast<uint16_t>(XCB_XKB_STATE_PART_MODIFIER_BASE | XCB_XKB_STATE_PART_MODIFIER_LATCH
                                | XCB_XKB_STATE_PART_MODIFIER_LOCK | XCB_XKB_STATE_PART_GROUP_BASE
                                | XCB_XKB_STATE_PART_GROUP_LATCH | XCB_XKB_STATE_PART_GROUP_LOCK);

    xcb_xkb_select_events_details_t const details = {
        .affectNewKeyboard = new_keyboard_details,
        .newKeyboardDetails = new_keyboard_details,
        .affectState = state_parts,
        .stateDetails = state_parts,
    };

    auto cookie = xcb_xkb_select_events_aux_checked(
        connection, device_id, selected_events, 0, 0, affect_map, affect_map, &details);

    auto error = xcb_request_check(connection, cookie);
    if (error) {
        free(error);
        return false;
    }

    return true;
}

template<typename Keyboard>
void xkb_update_keymap(Keyboard& keyboard)
{
    auto keymap = xkb_x11_keymap_new_from_device(keyboard.xkb->context,
                                                 keyboard.connection,
                                                 keyboard.xkb_device_id,
                                                 XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        qCWarning(KWIN_INPUT) << "xkb_x11_keymap_new_from_device failed";
        return;
    }

    auto state = xkb_x11_state_new_from_device(keymap, keyboard.connection, keyboard.xkb_device_id);
    if (!state) {
        xkb_keymap_unref(keymap);
        qCWarning(KWIN_INPUT) << "xkb_x11_state_new_from_device failed";
        return;
    }

    if (keyboard.xkb->state) {
        xkb_state_unref(keyboard.xkb->state);
    }

    keyboard.xkb->state = state;
    keyboard.xkb->keymap = std::make_shared<xkb::keymap>(keymap);
}

template<typename Keyboard>
bool xkb_handle_event(Keyboard& keyboard, xcb_generic_event_t* gen_event)
{
    union xkb_event {
        struct {
            uint8_t response_type;
            uint8_t xkbType;
            uint16_t sequence;
            xcb_timestamp_t time;
            uint8_t deviceID;
        } any;
        xcb_xkb_new_keyboard_notify_event_t new_keyboard_notify;
        xcb_xkb_map_notify_event_t map_notify;
        xcb_xkb_state_notify_event_t state_notify;
    }* event = reinterpret_cast<union xkb_event*>(gen_event);

    if (event->any.deviceID == keyboard.xkb_device_id) {
        switch (event->any.xkbType) {
        case XCB_XKB_NEW_KEYBOARD_NOTIFY:
            if (event->new_keyboard_notify.changed & XCB_XKB_NKN_DETAIL_KEYCODES) {
                xkb_update_keymap(keyboard);
            }
            break;

        case XCB_XKB_MAP_NOTIFY:
            xkb_update_keymap(keyboard);
            break;

        case XCB_XKB_STATE_NOTIFY:
            xkb_state_update_mask(keyboard.xkb->state,
                                  event->state_notify.baseMods,
                                  event->state_notify.latchedMods,
                                  event->state_notify.lockedMods,
                                  event->state_notify.baseGroup,
                                  event->state_notify.latchedGroup,
                                  event->state_notify.lockedGroup);
            break;
        }
    }

    return false;
}

}
