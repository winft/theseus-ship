/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"

#include <QObject>

namespace KWin::input
{

class cursor;
class idle_qobject;
class platform_qobject;

/// Only for exceptional use in environments without dependency injection support (e.g. Qt plugins).
struct KWIN_EXPORT singleton_interface {
    static input::cursor* cursor;
    static input::idle_qobject* idle_qobject;
    static input::platform_qobject* platform_qobject;
};

}
