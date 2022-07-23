/*
    SPDX-FileCopyrightText: 2021 Francesco Sorrentino <francesco.sorr@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/event.h"
#include "input/event_filter.h"

#include <Wrapland/Server/keyboard_pool.h>
#include <xkbcommon/xkbcommon.h>

namespace KWin::input
{

template<typename Redirect, typename KeyboardFilter>
class keyboard_grab : public event_filter<Redirect>
{
public:
    keyboard_grab(Redirect& redirect, KeyboardFilter* filter, xkb_keymap* keymap)
        : event_filter<Redirect>(redirect)
        , filter{filter}
        , keymap{xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1)}
    {
        // TODO(romangg): Should we throw when keymap is null?
        if (keymap) {
            filter->set_keymap(this->keymap);
        }
    }

    ~keyboard_grab()
    {
        if (keymap) {
            free(keymap);
        }
    }

    bool key(key_event const& event) override
    {
        filter->key(event.base.time_msec,
                    event.keycode,
                    event.state == key_state::pressed ? Wrapland::Server::key_state::pressed
                                                      : Wrapland::Server::key_state::released);
        return true;
    }

    bool key_repeat(key_event const& /*event*/) override
    {
        return true;
    }

private:
    KeyboardFilter* filter;
    char* keymap;
};

}
