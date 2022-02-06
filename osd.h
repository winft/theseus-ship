/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "utils/flags.h"

#include <QString>

namespace KWin::OSD
{

void show(QString const& message, QString const& iconName = QString());
void show(QString const& message, int timeout);
void show(QString const& message, QString const& iconName, int timeout);

enum class osd_hide_flags {
    none = 0x0,
    skip_close_animation = 0x1,
};

void hide(osd_hide_flags hide_flags = osd_hide_flags::none);

}

ENUM_FLAGS(KWin::OSD::osd_hide_flags)
