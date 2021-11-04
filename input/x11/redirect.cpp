/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "redirect.h"

#include "input/global_shortcuts_manager.h"
#include "input/keyboard_redirect.h"
#include "input/pointer_redirect.h"
#include "input/tablet_redirect.h"
#include "input/touch_redirect.h"

#include "main.h"
#include "seat/session.h"

namespace KWin::input::x11
{

redirect::redirect()
    : input::redirect(new input::keyboard_redirect(this),
                      new input::pointer_redirect,
                      new input::tablet_redirect,
                      new input::touch_redirect)
{
}

void redirect::install_shortcuts()
{
    m_shortcuts = std::make_unique<input::global_shortcuts_manager>();
    m_shortcuts->init();
}

}
