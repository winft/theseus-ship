/*
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QObject>
#include <memory>

namespace KWin::base::os::clock
{

class skew_notifier_engine : public QObject
{
    Q_OBJECT

public:
    static std::unique_ptr<skew_notifier_engine> create();

protected:
    skew_notifier_engine() = default;

Q_SIGNALS:
    void skewed();
};

}
