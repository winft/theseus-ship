/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "kwin_export.h"
#include "utils/flags.h"

#include <QString>

namespace KWin::win
{

class space;

KWIN_EXPORT void
osd_show(win::space& space, QString const& message, QString const& iconName = QString());
KWIN_EXPORT void osd_show(win::space& space, QString const& message, int timeout);
KWIN_EXPORT void
osd_show(win::space& space, QString const& message, QString const& iconName, int timeout);

enum class osd_hide_flags {
    none = 0x0,
    skip_close_animation = 0x1,
};

KWIN_EXPORT void osd_hide(win::space& space, osd_hide_flags hide_flags = osd_hide_flags::none);

}

ENUM_FLAGS(KWin::win::osd_hide_flags)
