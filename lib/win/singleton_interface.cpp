/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "singleton_interface.h"

namespace KWin::win
{

screen_edger_singleton* singleton_interface::edger{nullptr};
subspaces_singleton* singleton_interface::subspaces{nullptr};

std::function<QRect()> singleton_interface::get_current_output_geometry{};
std::function<std::string(std::string const&)> singleton_interface::set_activation_token{};
std::function<internal_window_singleton*(QWindow*)> singleton_interface::create_internal_window{};

}
