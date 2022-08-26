/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/redirect.h"

namespace KWin::input::x11
{

class keyboard_redirect;
class pointer_redirect;

class KWIN_EXPORT redirect : public input::redirect
{
public:
    redirect(input::platform& platform, win::space& space);
    ~redirect();

    input::keyboard_redirect* get_keyboard() const override;
    input::pointer_redirect* get_pointer() const override;
    input::tablet_redirect* get_tablet() const override;
    input::touch_redirect* get_touch() const override;

    std::unique_ptr<keyboard_redirect> keyboard;
    std::unique_ptr<pointer_redirect> pointer;
    std::unique_ptr<tablet_redirect> tablet;
    std::unique_ptr<touch_redirect> touch;
};

}
