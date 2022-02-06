/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QFlags>
#include <QString>

namespace KWin::OSD
{

void show(QString const& message, QString const& iconName = QString());
void show(QString const& message, int timeout);
void show(QString const& message, QString const& iconName, int timeout);

enum class HideFlag { SkipCloseAnimation = 1 };
Q_DECLARE_FLAGS(HideFlags, HideFlag)

void hide(HideFlags flags = HideFlags());

}
