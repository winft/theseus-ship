/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "redirect.h"

#include "input/keyboard_redirect.h"
#include "input/pointer_redirect.h"
#include "input/tablet_redirect.h"
#include "input/touch_redirect.h"

#include "main.h"

namespace KWin::input::x11
{

redirect::redirect(input::platform& platform, win::space& space)
    : input::redirect(platform, space)
{
    m_pointer = std::make_unique<input::pointer_redirect>(this);
    m_keyboard = std::make_unique<input::keyboard_redirect>(this);
    m_touch = std::make_unique<input::touch_redirect>(this);
    m_tablet = std::make_unique<input::tablet_redirect>(this);
}

}
