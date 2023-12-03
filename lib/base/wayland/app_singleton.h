/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <base/app_singleton.h>

namespace KWin::base::wayland
{

class app_singleton : public base::app_singleton
{
public:
    app_singleton(int& argc, char** argv)
    {
        setenv("QT_QPA_PLATFORM", "wayland-org.kde.kwin.qpa", true);
        setenv("KWIN_FORCE_OWN_QPA", "1", true);

        qunsetenv("QT_DEVICE_PIXEL_RATIO");
        qputenv("QSG_RENDER_LOOP", "basic");

        qapp = std::make_unique<QApplication>(argc, argv);
        prepare_qapp();

        // Reset QT_QPA_PLATFORM so we don't propagate it to our children (e.g. apps launched from
        // the overview effect).
        qunsetenv("QT_QPA_PLATFORM");
    }
};

}
