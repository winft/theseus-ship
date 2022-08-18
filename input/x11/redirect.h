/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "keyboard_redirect.h"
#include "pointer_redirect.h"

#include "input/redirect_qobject.h"

namespace KWin::input::x11
{

template<typename Platform>
class redirect
{
public:
    using type = redirect<Platform>;
    using space_t = typename Platform::base_t::space_t;
    using window_t = typename space_t::window_t;

    redirect(Platform& platform)
        : qobject{std::make_unique<redirect_qobject>()}
        , platform{platform}
    {
        platform.redirect = this;
        pointer = std::make_unique<pointer_redirect<type>>(this);
        keyboard = std::make_unique<keyboard_redirect<type>>(this);
    }

    ~redirect()
    {
        auto const spies = m_spies;
        for (auto spy : spies) {
            delete spy;
        }
    }

    std::unique_ptr<keyboard_redirect<type>> keyboard;
    std::unique_ptr<pointer_redirect<type>> pointer;

    std::vector<event_spy<type>*> m_spies;

    std::unique_ptr<redirect_qobject> qobject;
    Platform& platform;
};

}
