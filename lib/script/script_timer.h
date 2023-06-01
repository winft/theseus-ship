/*
    SPDX-FileCopyrightText: 2010 Rohan Prabhu <rohan@rohanprabhu.com>
    SPDX-FileCopyrightText: 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QTimer>

namespace KWin::scripting
{

/**
 * In order to be able to construct QTimer objects in javascript, the constructor
 * must be declared with Q_INVOKABLE.
 */
class script_timer : public QTimer
{
    Q_OBJECT

public:
    Q_INVOKABLE script_timer(QObject* parent = nullptr);
};

}
