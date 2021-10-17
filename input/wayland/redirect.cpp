/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "redirect.h"

#include "keyboard_redirect.h"
#include "pointer_redirect.h"

namespace KWin::input::wayland
{

redirect::redirect()
    : input::redirect(new keyboard_redirect(this), new pointer_redirect)
{
}

}
