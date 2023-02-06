/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "catch_macros.h"

#include "base/app_singleton.h"
#include "helpers.h"

#include <KCrash>
#include <QApplication>
#include <QPluginLoader>
#include <catch2/catch_session.hpp>

Q_IMPORT_PLUGIN(KWinIntegrationPlugin)
Q_IMPORT_PLUGIN(KWindowSystemKWinPlugin)
Q_IMPORT_PLUGIN(KWinIdleTimePoller)

int main(int argc, char* argv[])
{
    KCrash::setDrKonqiEnabled(false);
    KWin::Test::prepare_app_env(argv[0]);

    KWin::base::app_singleton app_singleton;
    QApplication app(argc, argv);

    app.setQuitOnLastWindowClosed(false);

    auto const own_path = app.libraryPaths().constLast();
    app.removeLibraryPath(own_path);
    app.addLibraryPath(own_path);

    return Catch::Session().run(argc, argv);
}
