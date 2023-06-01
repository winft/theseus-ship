/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "base/seat/session.h"
#include "input/event_filter.h"
#include "input/keyboard.h"
#include "input/keyboard_redirect.h"
#include "input/xkb/keyboard.h"

namespace KWin::input
{

template<typename Redirect>
class virtual_terminal_filter : public event_filter<Redirect>
{
public:
    explicit virtual_terminal_filter(Redirect& redirect)
        : event_filter<Redirect>(redirect)
    {
    }

    bool key(key_event const& event) override
    {
        // really on press and not on release? X11 switches on press.
        if (event.state == key_state::pressed) {
            auto const keysym = event.base.dev->xkb->to_keysym(event.keycode);
            if (keysym >= XKB_KEY_XF86Switch_VT_1 && keysym <= XKB_KEY_XF86Switch_VT_12) {
                this->redirect.platform.base.session->switchVirtualTerminal(
                    keysym - XKB_KEY_XF86Switch_VT_1 + 1);
                return true;
            }
        }
        return false;
    }
};

}
