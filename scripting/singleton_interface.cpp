/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "singleton_interface.h"

namespace KWin::scripting
{

scripting::qt_script_space* singleton_interface::qt_script_space{nullptr};
std::function<void(QKeySequence const& shortcut, QAction* action)>
    singleton_interface::register_shortcut{};

}
