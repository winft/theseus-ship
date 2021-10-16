/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/keyboard_redirect.h"

namespace KWin::input::wayland
{
class redirect;

class KWIN_EXPORT keyboard_redirect : public input::keyboard_redirect
{
    Q_OBJECT
public:
    explicit keyboard_redirect(wayland::redirect* redirect);
    ~keyboard_redirect() override;
};

}
