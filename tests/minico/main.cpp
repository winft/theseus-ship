/*
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include <base/app_singleton.h>
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

    KWin::base::app_singleton app_singleton;
    QApplication app(argc, argv);

    qunsetenv("QT_QPA_PLATFORM");

    app.setQuitOnLastWindowClosed(false);
    app.setQuitLockEnabled(false);

    KSignalHandler::self()->watchSignal(SIGTERM);
    KSignalHandler::self()->watchSignal(SIGINT);
    KSignalHandler::self()->watchSignal(SIGHUP);
    QObject::connect(
        KSignalHandler::self(), &KSignalHandler::signalReceived, &app, &QCoreApplication::exit);

    using base_t = base::wayland::platform<>;
    base_t base(base::config(KConfig::OpenFlag::FullConfig, "kwinft-minimalrc"),
                "",
                base::wayland::start_options::no_lock_screen_integration,
                base::backend::wlroots::start_options::none);
    base.operation_mode = base::operation_mode::wayland;
    base.options = base::create_options(base::operation_mode::wayland, base.config.main);

    try {
        base.render = std::make_unique<base_t::render_t>(base);
    } catch (std::system_error const& exc) {
        std::cerr << "FATAL ERROR: render creation failed: " << exc.what() << std::endl;
        exit(exc.code().value());
    }

    base.input = std::make_unique<input::wayland::platform<base_t>>(
        base, input::config(KConfig::NoGlobals));
    input::wayland::add_dbus(base.input.get());

    // TODO(romangg): remove
    base.input->install_shortcuts();

    try {
        base.render->init();
    } catch (std::exception const&) {
        std::cerr << "FATAL ERROR: backend failed to initialize, exiting now" << std::endl;
        QCoreApplication::exit(1);
    }

    base.space = std::make_unique<base_t::space_t>(*base.render, *base.input);
    base.space->desktop = std::make_unique<desktop::platform>(*base.space);

    base.render->start(*base.space);
    return app.exec();
}
