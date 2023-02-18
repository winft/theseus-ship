/*
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "skew_notifier_engine.h"

#if defined(Q_OS_LINUX)
#include "linux_skew_notifier_engine.h"
#endif

namespace KWin::base::os::clock
{

std::unique_ptr<skew_notifier_engine> skew_notifier_engine::create()
{
#if defined(Q_OS_LINUX)
    return linux_skew_notifier_engine::create();
#else
    return nullptr;
#endif
}

}
