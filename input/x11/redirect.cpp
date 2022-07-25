/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "redirect.h"

#include "keyboard_redirect.h"

#include "input/pointer_redirect.h"
#include "input/tablet_redirect.h"
#include "input/touch_redirect.h"

#include "main.h"

namespace KWin::input::x11
{

redirect::redirect(input::platform& platform, win::space& space)
    : input::redirect(platform, space)
{
    pointer = std::make_unique<input::pointer_redirect>(this);
    keyboard = std::make_unique<x11::keyboard_redirect>(this);
    touch = std::make_unique<input::touch_redirect>(this);
    tablet = std::make_unique<input::tablet_redirect>(this);
}

redirect::~redirect() = default;

input::keyboard_redirect* redirect::get_keyboard() const
{
    return keyboard.get();
}
input::pointer_redirect* redirect::get_pointer() const
{
    return pointer.get();
}
input::tablet_redirect* redirect::get_tablet() const
{
    return tablet.get();
}
input::touch_redirect* redirect::get_touch() const
{
    return touch.get();
}

}
