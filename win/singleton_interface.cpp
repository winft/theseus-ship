/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "singleton_interface.h"

namespace KWin::win
{

win::space* singleton_interface::space{nullptr};
std::function<std::string(std::string const&)> singleton_interface::set_activation_token{};

}
