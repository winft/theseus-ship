/*
SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "main.h"

#include <base/wayland/app_singleton.h>
#include <base/wayland/xwl_platform.h>
#include <desktop/kde/platform.h>
#include <render/shortcuts_init.h>
#include <script/platform.h>
#include <win/shortcuts_init.h>

#include <KShell>
#include <KSignalHandler>
#include <KUpdateLaunchEnvironmentJob>
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QProcess>
#include <sys/resource.h>

Q_IMPORT_PLUGIN(KWinIntegrationPlugin)
Q_IMPORT_PLUGIN(KWindowSystemKWinPlugin)
Q_IMPORT_PLUGIN(KWinIdleTimePoller)

namespace KWin
{

template<typename Base>
struct input_mod {
    using platform_t = input::wayland::platform<Base, input_mod>;
    std::unique_ptr<input::dbus::device_manager<platform_t>> dbus;
};

struct space_mod {
    std::unique_ptr<desktop::platform> desktop;
};

struct base_mod {
    using platform_t = base::wayland::xwl_platform<base_mod>;
    using render_t = render::wayland::xwl_platform<platform_t>;
    using input_t = input::wayland::platform<platform_t, input_mod<platform_t>>;
    using space_t = win::wayland::xwl_space<platform_t, space_mod>;

    std::unique_ptr<render_t> render;
    std::unique_ptr<input_t> input;
    std::unique_ptr<space_t> space;
    std::unique_ptr<xwl::xwayland<space_t>> xwayland;
    std::unique_ptr<scripting::platform<space_t>> script;
};

}

static rlimit originalNofileLimit = {
    .rlim_cur = 0,
    .rlim_max = 0,
};

namespace
{

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

struct exit_process_t {
    exit_process_t(QApplication& app)
        : app{&app}
    {
    }
    ~exit_process_t()
    {
        if (process && process->state() != QProcess::NotRunning) {
            QObject::disconnect(process, nullptr, app, nullptr);
            process->terminate();
            process->waitForFinished(5000);
        }
    }

    QApplication* app;
    QProcess* process{nullptr};
};

}

int main(int argc, char* argv[])
{
    using namespace KWin;

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
    bumpNofileLimit();

    signal(SIGPIPE, SIG_IGN);

    // ensure that no thread takes SIGUSR
    sigset_t userSignals;
    sigemptyset(&userSignals);
    sigaddset(&userSignals, SIGUSR1);
    sigaddset(&userSignals, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &userSignals, nullptr);

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

    base::wayland::app_singleton app(argc, argv);

    if (!Perf::Ftrace::setEnabled(qEnvironmentVariableIsSet("KWIN_PERF_FTRACE"))) {
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

    parser.process(*app.qapp);
    KAboutData::applicationData().processCommandLine(&parser);

    auto flags = base::wayland::start_options::none;
    if (parser.isSet(options.lockscreen)) {
        flags = base::wayland::start_options::lock_screen;
    } else if (!parser.isSet(options.no_lockscreen)) {
        flags = base::wayland::start_options::lock_screen_integration;
    }
    if (parser.isSet(options.no_global_shortcuts)) {
        flags |= base::wayland::start_options::no_global_shortcuts;
    }

    qDebug("Starting KWinFT (Wayland) %s", KWIN_VERSION_STRING);

    exit_process_t exit_process(*app.qapp);

    using base_t = base::wayland::xwl_platform<base_mod>;
    base_t base({
        .config = base::config(KConfig::OpenFlag::FullConfig, "kwinrc"),
        .socket_name = parser.value(options.socket).toStdString(),
        .flags = flags,
        .mode = parser.isSet(options.xwl) ? base::operation_mode::xwayland
                                          : base::operation_mode::wayland,
    });

    base.mod.render = std::make_unique<base_t::render_t>(base);

    base.mod.input = std::make_unique<base_t::input_t>(base, input::config(KConfig::NoGlobals));
    base.mod.input->mod.dbus
        = std::make_unique<input::dbus::device_manager<base_t::input_t>>(*base.mod.input);

    base.mod.space = std::make_unique<base_t::space_t>(*base.mod.render, *base.mod.input);
    base.mod.space->mod.desktop
        = std::make_unique<desktop::kde::platform<base_t::space_t>>(*base.mod.space);
    win::init_shortcuts(*base.mod.space);
    render::init_shortcuts(*base.mod.render);
    base.mod.script = std::make_unique<scripting::platform<base_t::space_t>>(*base.mod.space);

    base::wayland::platform_start(base);

    base.process_environment = QProcessEnvironment::systemEnvironment();

    if (auto const& name = base.server->display->socket_name(); !name.empty()) {
        base.process_environment.insert(QStringLiteral("WAYLAND_DISPLAY"), name.c_str());
    }

    base.server->init_screen_locker();

    if (base.operation_mode == base::operation_mode::xwayland) {
        try {
            base.mod.xwayland = std::make_unique<xwl::xwayland<base_t::space_t>>(*base.mod.space);
        } catch (std::system_error const& exc) {
            std::cerr << "FATAL ERROR creating Xwayland: " << exc.what() << std::endl;
            exit(exc.code().value());
        } catch (std::exception const& exc) {
            std::cerr << "FATAL ERROR creating Xwayland: " << exc.what() << std::endl;
            exit(1);
        }
    }

    auto process_environment = base.process_environment;

    // Enforce Wayland platform for started Qt apps. They otherwise for some reason prefer X11.
    process_environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("wayland"));

    // start session
    if (parser.isSet(options.exit_with_session) /*&& !m_sessionArgument.isEmpty()*/) {
        auto arguments = KShell::splitArgs(parser.value(options.exit_with_session));
        if (!arguments.isEmpty()) {
            QString program = arguments.takeFirst();
            auto p = new QProcess(app.qapp.get());
            p->setProcessChannelMode(QProcess::ForwardedErrorChannel);
            p->setProcessEnvironment(process_environment);
            QObject::connect(p,
                             qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                             app.qapp.get(),
                             [&exit_process, p](auto code, auto status) {
                                 exit_process.process = {};
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
            exit_process.process = p;
        } else {
            qWarning("Failed to launch the session process: %s is an invalid command",
                     qPrintable(parser.value(options.exit_with_session)));
        }
    }

    // start the applications passed to us as command line arguments
    if (auto apps = parser.positionalArguments(); !apps.isEmpty()) {
        for (auto const& app_name : qAsConst(apps)) {
            auto arguments = KShell::splitArgs(app_name);
            if (arguments.isEmpty()) {
                qWarning("Failed to launch application: %s is an invalid command",
                         qPrintable(app_name));
                continue;
            }
            QString program = arguments.takeFirst();
            // note: this will kill the started process when we exit
            // this is going to happen anyway as we are the wayland and X server the app connects to
            auto p = new QProcess(app.qapp.get());
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
    QObject::connect(env_sync_job, &KUpdateLaunchEnvironmentJob::finished, app.qapp.get(), []() {
        QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.KWinWrapper"));
    });

    return app.qapp->exec();
}
