/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <base/logging.h>

#include <KConfigGroup>
#include <QTimer>

namespace KWin::base::x11
{

template<typename Platform>
void platform_init_crash_count(Platform& platform, int& crash_count)
{
    platform.crash_count = crash_count;

    if (platform.crash_count >= 4) {
        // Something has gone seriously wrong
        qCDebug(KWIN_CORE) << "More than 3 crashes recently. Exiting now.";
        ::exit(1);
    }

    if (platform.crash_count >= 2) {
        // Disable compositing if we have had too many crashes
        qCDebug(KWIN_CORE) << "More than 1 crash recently. Disabling compositing.";
        KConfigGroup compgroup(KSharedConfig::openConfig(), "Compositing");
        compgroup.writeEntry("Enabled", false);
    }

    // Reset crashes count if we stay up for more that 15 seconds
    QTimer::singleShot(15 * 1000, platform.qobject.get(), [&] {
        platform.crash_count = 0;
        crash_count = 0;
    });
}

}
