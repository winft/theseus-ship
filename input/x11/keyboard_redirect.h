/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "redirect.h"

#include "input/keyboard_redirect.h"

#include <memory>

namespace KWin::input::x11
{

class keyboard_redirect : public input::keyboard_redirect
{
public:
    explicit keyboard_redirect(x11::redirect* redirect)
        : input::keyboard_redirect(redirect)
        , redirect{redirect}
    {
    }

    void update() override
    {
    }

    void process_key(key_event const& event) override
    {
        keyboard_redirect_prepare_key(*this, event);
    }

    void process_modifiers(modifiers_event const& /*event*/) override
    {
    }

    x11::redirect* redirect;
};

}
