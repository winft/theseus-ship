/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

namespace KWin::scripting
{

class platform;

/// Only for exceptional use in environments without dependency injection support (e.g. Qt plugins).
struct singleton_interface {
    static scripting::platform* platform;
};

}
