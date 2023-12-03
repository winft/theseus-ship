/*
SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "main_wayland.h"

#include "main.h"

#include "base/wayland/server.h"
#include "input/wayland/cursor.h"
#include "input/wayland/redirect.h"
#include "render/effects.h"
#include "render/shortcuts_init.h"
#include "script/platform.h"
#include "win/shortcuts_init.h"
#include "win/wayland/space.h"
#include "xwl/xwayland.h"
#include <base/wayland/app_singleton.h>
#include <desktop/kde/platform.h>
#include <input/wayland/platform.h>
#include <render/wayland/xwl_platform.h>

// Wrapland
#include <Wrapland/Server/display.h>
#include <Wrapland/Server/seat.h>
// KDE
#include <KLocalizedString>
#include <KPluginMetaData>
#include <KShell>
#include <KSignalHandler>
#include <KUpdateLaunchEnvironmentJob>

// Qt
#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDebug>
#include <QFileInfo>
#include <QProcess>
#include <QWindow>
#include <qplatformdefs.h>

#include <sched.h>
#include <sys/resource.h>

#include <iomanip>
#include <iostream>

Q_IMPORT_PLUGIN(KWinIntegrationPlugin)
Q_IMPORT_PLUGIN(KWindowSystemKWinPlugin)
Q_IMPORT_PLUGIN(KWinIdleTimePoller)

namespace KWin
{

static rlimit originalNofileLimit = {
    .rlim_cur = 0,
    .rlim_max = 0,
};

static void restoreNofileLimit()
{
    if (setrlimit(RLIMIT_NOFILE, &originalNofileLimit) == -1) {
        std::cerr << "Failed to restore RLIMIT_NOFILE limit, legacy apps might be broken"
                  << std::endl;
    }
}

static void bumpNofileLimit()
{
    // It's easy to exceed the file descriptor limit because many things are backed using fds
    // nowadays, e.g. dmabufs, shm buffers, etc. Bump the RLIMIT_NOFILE limit to handle that.
    // Some apps may still use select(), so we reset the limit to its original value in fork().

    if (getrlimit(RLIMIT_NOFILE, &originalNofileLimit) == -1) {
        std::cerr << "Failed to bump RLIMIT_NOFILE limit, getrlimit() failed: " << strerror(errno)
                  << std::endl;
        return;
    }

    rlimit limit = originalNofileLimit;
    limit.rlim_cur = limit.rlim_max;

    if (setrlimit(RLIMIT_NOFILE, &limit) == -1) {
        std::cerr << "Failed to bump RLIMIT_NOFILE limit, setrlimit() failed: " << strerror(errno)
                  << std::endl;
        return;
    }

    pthread_atfork(nullptr, nullptr, restoreNofileLimit);
}

//************************************
// ApplicationWayland
//************************************

ApplicationWayland::ApplicationWayland(QApplication& app)
    : app{&app}
{
    app_init();
}

ApplicationWayland::~ApplicationWayland()
{
    if (exit_with_process && exit_with_process->state() != QProcess::NotRunning) {
        QObject::disconnect(exit_with_process, nullptr, app, nullptr);
        exit_with_process->terminate();
        exit_with_process->waitForFinished(5000);
        exit_with_process = nullptr;
    }
}

void ApplicationWayland::start(base::operation_mode mode,
                               std::string const& socket_name,
                               base::wayland::start_options flags,
                               QProcessEnvironment environment)
{
    assert(mode != base::operation_mode::x11);

    base = std::make_unique<base_t>(base::wayland::platform_arguments{
        .config = base::config(KConfig::OpenFlag::FullConfig, "kwinrc"),
        .socket_name = socket_name,
        .mode = mode,
        .flags = flags,
    });

    base->mod.render = std::make_unique<base_t::render_t>(*base);

    base->mod.input = std::make_unique<base_t::input_t>(*base, input::config(KConfig::NoGlobals));
    base->mod.input->mod.dbus
        = std::make_unique<input::dbus::device_manager<base_t::input_t>>(*base->mod.input);

    base->mod.space = std::make_unique<base_t::space_t>(*base->mod.render, *base->mod.input);
    base->mod.space->mod.desktop
        = std::make_unique<desktop::kde::platform<base_t::space_t>>(*base->mod.space);
    win::init_shortcuts(*base->mod.space);
    render::init_shortcuts(*base->mod.render);
    base->mod.script = std::make_unique<scripting::platform<base_t::space_t>>(*base->mod.space);

    base::wayland::platform_start(*base);

    if (auto const& name = base->server->display->socket_name(); !name.empty()) {
        environment.insert(QStringLiteral("WAYLAND_DISPLAY"), name.c_str());
    }

    base->process_environment = environment;
    base->server->init_screen_locker();

    if (base->operation_mode == base::operation_mode::xwayland) {
        create_xwayland();
    } else {
        startSession();
    }
}

void ApplicationWayland::create_xwayland()
{
    auto status_callback = [this](auto error) {
        if (error) {
            // we currently exit on Xwayland errors always directly
            // TODO: restart Xwayland
            std::cerr << "Xwayland had a critical error. Going to exit now." << std::endl;
            exit(error);
        }
        startSession();
    };

    try {
        base->mod.xwayland
            = std::make_unique<xwl::xwayland<base_t::space_t>>(*base->mod.space, status_callback);
    } catch (std::system_error const& exc) {
        std::cerr << "FATAL ERROR creating Xwayland: " << exc.what() << std::endl;
        exit(exc.code().value());
    } catch (std::exception const& exc) {
        std::cerr << "FATAL ERROR creating Xwayland: " << exc.what() << std::endl;
        exit(1);
    }
}

void ApplicationWayland::startSession()
{
    auto process_environment = base->process_environment;

    // Enforce Wayland platform for started Qt apps. They otherwise for some reason prefer X11.
    process_environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("wayland"));

    // start session
    if (!m_sessionArgument.isEmpty()) {
        QStringList arguments = KShell::splitArgs(m_sessionArgument);
        if (!arguments.isEmpty()) {
            QString program = arguments.takeFirst();
            auto p = new QProcess(app);
            p->setProcessChannelMode(QProcess::ForwardedErrorChannel);
            p->setProcessEnvironment(process_environment);
            QObject::connect(p,
                             qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                             app,
                             [this, p](int code, QProcess::ExitStatus status) {
                                 exit_with_process = nullptr;
                                 p->deleteLater();
                                 if (status == QProcess::CrashExit) {
                                     qWarning() << "Session process has crashed";
                                     QCoreApplication::exit(-1);
                                     return;
                                 }

                                 if (code) {
                                     qWarning() << "Session process exited with code" << code;
                                 }

                                 QCoreApplication::exit(code);
                             });
            p->setProgram(program);
            p->setArguments(arguments);
            p->start();
            exit_with_process = p;
        } else {
            qWarning("Failed to launch the session process: %s is an invalid command",
                     qPrintable(m_sessionArgument));
        }
    }
    // start the applications passed to us as command line arguments
    if (!m_applicationsToStart.isEmpty()) {
        for (const QString& application : qAsConst(m_applicationsToStart)) {
            QStringList arguments = KShell::splitArgs(application);
            if (arguments.isEmpty()) {
                qWarning("Failed to launch application: %s is an invalid command",
                         qPrintable(application));
                continue;
            }
            QString program = arguments.takeFirst();
            // note: this will kill the started process when we exit
            // this is going to happen anyway as we are the wayland and X server the app connects to
            auto p = new QProcess(app);
            p->setProcessChannelMode(QProcess::ForwardedErrorChannel);
            p->setProcessEnvironment(process_environment);
            p->setProgram(program);
            p->setArguments(arguments);
            p->startDetached();
            p->deleteLater();
        }
    }

    // Need to create a launch environment job for Plasma components to catch up in a systemd boot.
    // This implies we're running in a full Plasma session i.e. when we use the wrapper (that's
    // there the service name comes from), but we can also do it in a plain setup without session.
    // Registering the service names indicates that we're live and all env vars are exported.
    auto env_sync_job = new KUpdateLaunchEnvironmentJob(process_environment);
    QObject::connect(env_sync_job, &KUpdateLaunchEnvironmentJob::finished, app, []() {
        QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.KWinWrapper"));
    });
}

} // namespace

int main(int argc, char* argv[])
{
    // Redirect stderr output. This is useful as a workaround for missing logs in systemd journal
    // when launching a full Plasma session.
    if (auto log_path = getenv("KWIN_LOG_PATH")) {
        if (!freopen(log_path, "w", stderr)) {
            std::cerr << "Failed to open '" << log_path << "' for writing stderr." << std::endl;
            return 1;
        }
    }

    if (getuid() == 0) {
        std::cerr << "kwin_wayland does not support running as root." << std::endl;
        return 1;
    }

    KLocalizedString::setApplicationDomain("kwin");
    KWin::bumpNofileLimit();

    signal(SIGPIPE, SIG_IGN);

    // ensure that no thread takes SIGUSR
    sigset_t userSignals;
    sigemptyset(&userSignals);
    sigaddset(&userSignals, SIGUSR1);
    sigaddset(&userSignals, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &userSignals, nullptr);

    auto environment = QProcessEnvironment::systemEnvironment();

    KWin::base::wayland::app_singleton app(argc, argv);
    KWin::ApplicationWayland a(*app.qapp);

    KSignalHandler::self()->watchSignal(SIGTERM);
    KSignalHandler::self()->watchSignal(SIGINT);
    KSignalHandler::self()->watchSignal(SIGHUP);
    QObject::connect(KSignalHandler::self(),
                     &KSignalHandler::signalReceived,
                     app.qapp.get(),
                     &QCoreApplication::exit);

    KWin::app_create_about_data();

    struct {
        QCommandLineOption xwl = {
            QStringLiteral("xwayland"),
            i18n("Start a rootless Xwayland server."),
        };
        QCommandLineOption socket = {
            QStringList{QStringLiteral("s"), QStringLiteral("socket")},
            i18n("Name of the Wayland socket to listen on. If not set \"wayland-0\" is used."),
            QStringLiteral("socket"),
        };
        QCommandLineOption lockscreen = {
            QStringLiteral("lockscreen"),
            i18n("Starts the session in locked mode."),
        };
        QCommandLineOption no_lockscreen = {
            QStringLiteral("no-lockscreen"),
            i18n("Starts the session without lock screen support."),
        };
        QCommandLineOption no_global_shortcuts = {
            QStringLiteral("no-global-shortcuts"),
            i18n("Starts the session without global shortcuts support."),
        };
        QCommandLineOption exit_with_session = {
            QStringLiteral("exit-with-session"),
            i18n("Exit after the session application, which is started by KWin, closed."),
            QStringLiteral("/path/to/session"),
        };
    } options;

    QCommandLineParser parser;
    parser.setApplicationDescription(i18n("KWinFT Wayland Window Manager"));
    KAboutData::applicationData().setupCommandLine(&parser);

    parser.addOption(options.xwl);
    parser.addOption(options.socket);
    parser.addOption(options.no_lockscreen);
    parser.addOption(options.no_global_shortcuts);
    parser.addOption(options.lockscreen);
    parser.addOption(options.exit_with_session);
    parser.addPositionalArgument(QStringLiteral("applications"),
                                 i18n("Applications to start once server is started"),
                                 QStringLiteral("[/path/to/application...]"));

    parser.process(*app.qapp);
    KAboutData::applicationData().processCommandLine(&parser);

    if (parser.isSet(options.exit_with_session)) {
        a.setSessionArgument(parser.value(options.exit_with_session));
    }

    auto flags = KWin::base::wayland::start_options::none;
    if (parser.isSet(options.lockscreen)) {
        flags = KWin::base::wayland::start_options::lock_screen;
    } else if (!parser.isSet(options.no_lockscreen)) {
        flags = KWin::base::wayland::start_options::lock_screen_integration;
    }
    if (parser.isSet(options.no_global_shortcuts)) {
        flags |= KWin::base::wayland::start_options::no_global_shortcuts;
    }

    auto op_mode = parser.isSet(options.xwl) ? KWin::base::operation_mode::xwayland
                                             : KWin::base::operation_mode::wayland;

    a.setApplicationsToStart(parser.positionalArguments());

    qDebug("Starting KWinFT (Wayland) %s", KWIN_VERSION_STRING);
    a.start(op_mode, parser.value(options.socket).toStdString(), flags, environment);

    return app.qapp->exec();
}
