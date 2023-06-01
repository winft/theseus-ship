/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QAction>
#include <QKeySequence>
#include <functional>

namespace KWin::scripting
{

class qt_script_space;

/// Only for exceptional use in environments without dependency injection support (e.g. Qt plugins).
struct KWIN_EXPORT singleton_interface {
    static scripting::qt_script_space* qt_script_space;
    static std::function<void(QKeySequence const& shortcut, QAction* action)> register_shortcut;
};

}
