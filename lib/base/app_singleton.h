/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QApplication>
#include <QObject>
#include <kwin_export.h>

namespace KWin::base
{

class KWIN_EXPORT app_singleton : public QObject
{
    Q_OBJECT
public:
    app_singleton(int& argc, char** argv);
    std::unique_ptr<QApplication> qapp;

Q_SIGNALS:
    void platform_created();
};

}
