/*
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include <base/wayland/app_singleton.h>
#include <base/wayland/platform.h>
#include <desktop/platform.h>
#include <input/wayland/platform.h>
#include <script/platform.h>

#include <KCrash>
#include <KLocalizedString>
#include <KSignalHandler>
#include <QApplication>
#include <iostream>

Q_IMPORT_PLUGIN(KWinIntegrationPlugin)
Q_IMPORT_PLUGIN(KWindowSystemKWinPlugin)
Q_IMPORT_PLUGIN(KWinIdleTimePoller)

int main(int argc, char* argv[])
{
    using namespace KWin;

    KCrash::setDrKonqiEnabled(false);
    KLocalizedString::setApplicationDomain("kwin");

    signal(SIGPIPE, SIG_IGN);

    // ensure that no thread takes SIGUSR
    sigset_t userSignals;
    sigemptyset(&userSignals);
    sigaddset(&userSignals, SIGUSR1);
    sigaddset(&userSignals, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &userSignals, nullptr);

    setenv("QT_QPA_PLATFORM", "wayland-org.kde.kwin.qpa", true);
    setenv("KWIN_FORCE_OWN_QPA", "1", true);

    qunsetenv("QT_DEVICE_PIXEL_RATIO");
    qputenv("QSG_RENDER_LOOP", "basic");

    KWin::base::wayland::app_singleton app(argc, argv);

    qunsetenv("QT_QPA_PLATFORM");

    KSignalHandler::self()->watchSignal(SIGTERM);
    KSignalHandler::self()->watchSignal(SIGINT);
    KSignalHandler::self()->watchSignal(SIGHUP);
    QObject::connect(KSignalHandler::self(),
                     &KSignalHandler::signalReceived,
                     app.qapp.get(),
                     &QCoreApplication::exit);

    using base_t = base::wayland::platform<>;
    base_t base({.config = base::config(KConfig::OpenFlag::FullConfig, "kwinft-minimalrc")});
    base.options = base::create_options(base::operation_mode::wayland, base.config.main);

    base.mod.render = std::make_unique<base_t::render_t>(base);
    base.mod.input = std::make_unique<input::wayland::platform<base_t>>(
        base, input::config(KConfig::NoGlobals));
    base.mod.space = std::make_unique<base_t::space_t>(*base.mod.render, *base.mod.input);

    return base::wayland::exec(base, app);
}
