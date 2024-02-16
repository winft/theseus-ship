/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "main.h"

#include <como/base/seat/backend/logind/session.h>
#include <como/base/x11/app_singleton.h>
#include <como/base/x11/platform.h>
#include <como/base/x11/platform_helpers.h>
#include <como/debug/perf/ftrace.h>
#include <como/desktop/kde/platform.h>
#include <como/render/backend/x11/platform.h>
#include <como/render/shortcuts_init.h>
#include <como/script/platform.h>
#include <como/win/shortcuts_init.h>

#include <KCrash>
#include <KSignalHandler>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QFile>
#include <iostream>

namespace
{

int crash_count = 0;

void notify_ksplash()
{
    // Tell KSplash that KWin has started
    auto ksplash_progress_message
        = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KSplash"),
                                         QStringLiteral("/KSplash"),
                                         QStringLiteral("org.kde.KSplash"),
                                         QStringLiteral("setStage"));
    ksplash_progress_message.setArguments(QList<QVariant>() << QStringLiteral("wm"));
    QDBusConnection::sessionBus().asyncCall(ksplash_progress_message);
}

void crash_handler(int signal)
{
    crash_count++;

    fprintf(
        stderr, "crash_handler() called with signal %d; recent crashes: %d\n", signal, crash_count);
    char cmd[1024];
    sprintf(cmd,
            "%s --crashes %d &",
            QFile::encodeName(QCoreApplication::applicationFilePath()).constData(),
            crash_count);

    sleep(1);
    system(cmd);
}

}

namespace theseus_ship
{

struct space_mod {
    std::unique_ptr<como::desktop::platform> desktop;
};

struct base_mod {
    using platform_t = como::base::x11::platform<base_mod>;
    using render_t = como::render::x11::platform<platform_t>;
    using input_t = como::input::x11::platform<platform_t>;
    using space_t = como::win::x11::space<platform_t, space_mod>;

    std::unique_ptr<render_t> render;
    std::unique_ptr<input_t> input;
    std::unique_ptr<space_t> space;
    std::unique_ptr<como::scripting::platform<space_t>> script;
};

}

int main(int argc, char* argv[])
{
    using namespace theseus_ship;

    KLocalizedString::setApplicationDomain("kwin");

    signal(SIGPIPE, SIG_IGN);

    como::base::x11::app_singleton app(argc, argv);

    if (!como::Perf::Ftrace::setEnabled(qEnvironmentVariableIsSet("KWIN_PERF_FTRACE"))) {
        qWarning() << "Can't enable Ftrace via environment variable.";
    }

    KSignalHandler::self()->watchSignal(SIGTERM);
    KSignalHandler::self()->watchSignal(SIGINT);
    KSignalHandler::self()->watchSignal(SIGHUP);
    QObject::connect(KSignalHandler::self(),
                     &KSignalHandler::signalReceived,
                     app.qapp.get(),
                     &QCoreApplication::exit);

    app_create_about_data();

    QCommandLineOption crashesOption(
        "crashes", i18n("Indicate that KWin has recently crashed n times"), QStringLiteral("n"));
    QCommandLineOption replaceOption(
        QStringLiteral("replace"),
        i18n("Replace already-running ICCCM2.0-compliant window manager"));

    QCommandLineParser parser;
    parser.setApplicationDescription(i18n("Theseus' Ship X11 Window Manager"));
    KAboutData::applicationData().setupCommandLine(&parser);

    parser.addOption(crashesOption);
    parser.addOption(replaceOption);

    parser.process(*app.qapp);

    qDebug("Starting Theseus' Ship (X11) %s", "0.0.0");

    KAboutData::applicationData().processCommandLine(&parser);
    crash_count = parser.value("crashes").toInt();

    using base_t = como::base::x11::platform<base_mod>;
    base_t base(como::base::config(KConfig::OpenFlag::FullConfig, "kwinrc"));

    KCrash::setEmergencySaveFunction(crash_handler);
    como::base::x11::platform_init_crash_count(base, crash_count);

    auto handle_ownership_claimed = [&base] {
        base.options
            = como::base::create_options(como::base::operation_mode::x11, base.config.main);

        // Check  whether another windowmanager is running
        const uint32_t maskValues[] = {XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT};
        como::unique_cptr<xcb_generic_error_t> redirectCheck(
            xcb_request_check(base.x11_data.connection,
                              xcb_change_window_attributes_checked(base.x11_data.connection,
                                                                   base.x11_data.root_window,
                                                                   XCB_CW_EVENT_MASK,
                                                                   maskValues)));
        if (redirectCheck) {
            fputs(i18n("kwin: another window manager is running (try using --replace)\n")
                      .toLocal8Bit()
                      .constData(),
                  stderr);
            // If this is a crash-restart, DrKonqi may have stopped the process w/o killing the
            // connection.
            if (base.crash_count == 0) {
                ::exit(1);
            }
        }

        base.session = std::make_unique<como::base::seat::backend::logind::session>();
        base.mod.render = std::make_unique<como::render::backend::x11::platform<base_t>>(base);
        base.mod.input = std::make_unique<como::input::x11::platform<base_t>>(base);

        base.update_outputs();
        auto render
            = static_cast<como::render::backend::x11::platform<base_t>*>(base.mod.render.get());
        try {
            render->init();
        } catch (std::exception const&) {
            std::cerr << "FATAL ERROR: backend failed to initialize, exiting now" << std::endl;
            ::exit(1);
        }

        try {
            base.mod.space = std::make_unique<base_t::space_t>(*base.mod.render, *base.mod.input);
        } catch (std::exception& ex) {
            qCCritical(KWIN_CORE) << "Abort since space creation fails with:" << ex.what();
            exit(1);
        }

        base.mod.space->mod.desktop
            = std::make_unique<como::desktop::kde::platform<base_t::space_t>>(*base.mod.space);
        como::win::init_shortcuts(*base.mod.space);
        como::render::init_shortcuts(*base.mod.render);

        base.mod.script
            = std::make_unique<como::scripting::platform<base_t::space_t>>(*base.mod.space);
        render->start(*base.mod.space);

        // Trigger possible errors, there's still a chance to abort.
        como::base::x11::xcb::sync(base.x11_data.connection);
        notify_ksplash();
    };

    como::base::x11::platform_start(base, parser.isSet(replaceOption), handle_ownership_claimed);

    return app.qapp->exec();
}
