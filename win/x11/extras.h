/*
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "net/net.h"

#include <QObject>
#include <QPixmap>

namespace KWin::win::x11
{

namespace net
{
class win_info;
}

class KWIN_EXPORT extras : public QObject
{
    Q_OBJECT
public:
    static WId activeWindow();
    static void activateWindow(WId win, long time = 0);
    static void forceActiveWindow(WId win, long time = 0);

    static bool compositingActive();

    static int currentDesktop();
    static void setCurrentDesktop(int desktop);
    static void setOnAllDesktops(WId win, bool b);
    static void setOnDesktop(WId win, int desktop);

    enum IconSource {
        NETWM = 1,     //!< read from property from the window manager specification
        WMHints = 2,   //!< read from WMHints property
        ClassHint = 4, //!< load icon after getting name from the classhint
        XApp = 8,      //!< load the standard X icon (last fallback)
    };

    static QPixmap icon(net::win_info const& info, int width, int height, bool scale, int flags);

    static void minimizeWindow(WId win);
    static void unminimizeWindow(WId win);

    static QRect workArea(int desktop = -1);
    static QRect workArea(const QList<WId>& excludes, int desktop = -1);

    static QString desktopName(int desktop);
    static void setDesktopName(int desktop, const QString& name);

    static QString readNameProperty(WId window, unsigned long atom);

    static void setExtendedStrut(WId win,
                                 int left_width,
                                 int left_start,
                                 int left_end,
                                 int right_width,
                                 int right_start,
                                 int right_end,
                                 int top_width,
                                 int top_start,
                                 int top_end,
                                 int bottom_width,
                                 int bottom_start,
                                 int bottom_end);
    static void setStrut(WId win, int left, int right, int top, int bottom);
};

}
