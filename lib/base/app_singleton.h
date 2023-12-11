/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <base/singleton_interface.h>

#include <QApplication>
#include <QObject>
#include <kwin_export.h>

namespace KWin::base
{

class KWIN_EXPORT app_singleton : public QObject
{
    Q_OBJECT
public:
    std::unique_ptr<QApplication> qapp;

protected:
    app_singleton()
    {
        qunsetenv("QT_DEVICE_PIXEL_RATIO");

        singleton_interface::app_singleton = this;
    }

    void prepare_qapp()
    {
        qapp->setQuitOnLastWindowClosed(false);
        qapp->setQuitLockEnabled(false);
    }

Q_SIGNALS:
    void platform_created();
};

}
