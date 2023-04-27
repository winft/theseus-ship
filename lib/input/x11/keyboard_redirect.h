/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/keyboard_redirect.h"

#include <memory>

namespace KWin::input::x11
{

template<typename Redirect>
class keyboard_redirect
{
public:
    explicit keyboard_redirect(Redirect* redirect)
        : qobject{std::make_unique<keyboard_redirect_qobject>()}
        , redirect{redirect}
    {
    }

    void update()
    {
    }

    void process_key(key_event const& event)
    {
        keyboard_redirect_prepare_key<Redirect>(*this, event);
    }

    void process_modifiers(modifiers_event const& /*event*/)
    {
    }

    std::unique_ptr<keyboard_redirect_qobject> qobject;
    Redirect* redirect;
};

}
