/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <base/app_singleton.h>
#include <base/config-kwin.h>

namespace KWin::base::wayland
{

class app_singleton : public base::app_singleton
{
public:
    app_singleton(int& argc, char** argv)
    {
        setenv("QT_QPA_PLATFORM", "wayland-org.kde.kwin.qpa", true);
        setenv("KWIN_FORCE_OWN_QPA", "1", true);

#if HAVE_SCHED_RESET_ON_FORK
        int const minPriority = sched_get_priority_min(SCHED_RR);
        sched_param sp;
        sp.sched_priority = minPriority;
        sched_setscheduler(0, SCHED_RR | SCHED_RESET_ON_FORK, &sp);
#endif

        qapp = std::make_unique<QApplication>(argc, argv);
        prepare_qapp();

        // Reset QT_QPA_PLATFORM so we don't propagate it to our children (e.g. apps launched from
        // the overview effect).
        qunsetenv("QT_QPA_PLATFORM");
    }
};

}
