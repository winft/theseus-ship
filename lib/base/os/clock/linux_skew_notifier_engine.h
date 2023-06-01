/*
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "skew_notifier_engine.h"

namespace KWin::base::os::clock
{

class linux_skew_notifier_engine : public skew_notifier_engine
{
    Q_OBJECT

public:
    linux_skew_notifier_engine(int fd);
    ~linux_skew_notifier_engine() override;

    static std::unique_ptr<linux_skew_notifier_engine> create();

private Q_SLOTS:
    void handle_timer_cancelled();

private:
    int m_fd;
};

}
