/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard_redirect.h"

#include "redirect.h"

#include "wayland_server.h"

namespace KWin::input::wayland
{

keyboard_redirect::keyboard_redirect(wayland::redirect* redirect)
    : input::keyboard_redirect(redirect)
{
    m_xkb->setSeat(waylandServer()->seat());
}

keyboard_redirect::~keyboard_redirect() = default;

}
