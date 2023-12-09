/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <base/app_singleton.h>

#include <QSurfaceFormat>
#include <QtGui/private/qtx11extras_p.h>
#include <xcb/xcb.h>

namespace KWin::base::x11
{

class app_singleton : public base::app_singleton
{
public:
    app_singleton(int& argc, char** argv)
    {
        int primaryScreen = 0;
        auto con = xcb_connect(nullptr, &primaryScreen);
        if (!con || xcb_connection_has_error(con)) {
            fprintf(stderr,
                    "%s: FATAL ERROR while trying to open display %s\n",
                    argv[0],
                    qgetenv("DISPLAY").constData());
            exit(1);
        }

        xcb_disconnect(con);
        con = nullptr;

        // enforce xcb plugin, unfortunately command line switch has precedence
        setenv("QT_QPA_PLATFORM", "xcb", true);

        // disable highdpi scaling
        setenv("QT_ENABLE_HIGHDPI_SCALING", "0", true);

        qunsetenv("QT_SCALE_FACTOR");
        qunsetenv("QT_SCREEN_SCALE_FACTORS");

        // KSMServer talks to us directly on DBus.
        QCoreApplication::setAttribute(Qt::AA_DisableSessionManager);
        // For sharing thumbnails between our scene graph and qtquick.
        QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

        QSurfaceFormat format = QSurfaceFormat::defaultFormat();
        // shared opengl contexts must have the same reset notification policy
        format.setOptions(QSurfaceFormat::ResetNotification);
        // disables vsync for any QtQuick windows we create (BUG 406180)
        format.setSwapInterval(0);
        QSurfaceFormat::setDefaultFormat(format);

        qapp = std::make_unique<QApplication>(argc, argv);
        prepare_qapp();

        // Reset QT_QPA_PLATFORM so we don't propagate it to our children (e.g. apps launched from
        // the overview effect).
        qunsetenv("QT_QPA_PLATFORM");
        qunsetenv("QT_ENABLE_HIGHDPI_SCALING");

        // Perform sanity checks.
        if (qapp->platformName().toLower() != QStringLiteral("xcb")) {
            fprintf(stderr,
                    "%s: FATAL ERROR expecting platform xcb but got platform %s\n",
                    argv[0],
                    qPrintable(qapp->platformName()));
            exit(1);
        }

        if (!QX11Info::display()) {
            fprintf(
                stderr,
                "%s: FATAL ERROR KWin requires Xlib support in the xcb plugin. Do not configure Qt "
                "with -no-xcb-xlib\n",
                argv[0]);
            exit(1);
        }
    }
};

}
