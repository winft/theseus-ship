/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "render/types.h"
#include "utils/flags.h"

namespace KWin::render::x11
{

enum class suspend_reason {
    none = 0,
    user = 1 << 0,
    rule = 1 << 1,
    script = 1 << 2,
    all = 0xff,
};

/// Whether to keep all windows mapped when compositing (i.e. whether to have actively updated
/// window pixmaps).
enum class hidden_preview {
    /// The normal mode with regard to mapped windows. Hidden (minimized, etc.) and windows on
    /// inactive virtual desktops are not mapped, their pixmaps are only their icons.
    never,
    /// Like normal mode, but shown windows (i.e. on inactive virtual desktops) are kept mapped,
    /// only hidden windows are unmapped.
    shown,
    /// All windows are kept mapped regardless of their state.
    always,
};

}

ENUM_FLAGS(KWin::render::x11::suspend_reason)
