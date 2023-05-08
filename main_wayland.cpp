/*
SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "main_wayland.h"

#include "config-kwin.h"
#include "main.h"

#include "base/app_singleton.h"
#include "base/backend/wlroots/platform.h"
#include "base/seat/backend/wlroots/session.h"
#include "base/wayland/server.h"
#include "desktop/screen_locker_watcher.h"
#include "render/backend/wlroots/platform.h"
#include "render/effects.h"
#include "render/wayland/compositor.h"
#include "input/backend/wlroots/platform.h"
#include "input/wayland/cursor.h"
#include "input/wayland/platform.h"
#include "input/wayland/redirect.h"
#include "script/platform.h"
#include "win/shortcuts_init.h"
#include "win/wayland/space.h"
#include "xwl/xwayland.h"

// Wrapland
#include <Wrapland/Server/display.h>
#include <Wrapland/Server/seat.h>
// KDE
#include <KCrash>
#include <KLocalizedString>
#include <KPluginMetaData>
#include <KShell>
#include <KSignalHandler>
#include <KUpdateLaunchEnvironmentJob>

// Qt
#include <qplatformdefs.h>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QFileInfo>
#include <QProcess>
#include <QDebug>
#include <QWindow>

#include <sched.h>
#include <sys/resource.h>

#include <iostream>
#include <iomanip>

Q_IMPORT_PLUGIN(KWinIntegrationPlugin)
Q_IMPORT_PLUGIN(KWindowSystemKWinPlugin)
Q_IMPORT_PLUGIN(KWinIdleTimePoller)

namespace KWin
{

static rlimit originalNofileLimit = {
    .rlim_cur = 0,
    .rlim_max = 0,
};

static bool bumpNofileLimit()
{
    if (getrlimit(RLIMIT_NOFILE, &originalNofileLimit) == -1) {
        std::cerr << "Failed to bump RLIMIT_NOFILE limit, getrlimit() failed: " << strerror(errno)
                  << std::endl;
        return false;
    }

    rlimit limit = originalNofileLimit;
    limit.rlim_cur = limit.rlim_max;

    if (setrlimit(RLIMIT_NOFILE, &limit) == -1) {
        std::cerr << "Failed to bump RLIMIT_NOFILE limit, setrlimit() failed: " << strerror(errno)
                  << std::endl;
        return false;
    }

    return true;
}

static void restoreNofileLimit()
{
    if (setrlimit(RLIMIT_NOFILE, &originalNofileLimit) == -1) {
        std::cerr << "Failed to restore RLIMIT_NOFILE limit, legacy apps might be broken"
                  << std::endl;
    }
}

void disableDrKonqi()
{
    KCrash::setDrKonqiEnabled(false);
}
// run immediately, before Q_CORE_STARTUP functions
// that would enable drkonqi
Q_CONSTRUCTOR_FUNCTION(disableDrKonqi)

enum class RealTimeFlags
{
    DontReset,
    ResetOnFork
};

namespace {
void gainRealTime()
{
#if HAVE_SCHED_RESET_ON_FORK
    const int minPriority = sched_get_priority_min(SCHED_RR);
    sched_param sp;
    sp.sched_priority = minPriority;
    sched_setscheduler(0, SCHED_RR | SCHED_RESET_ON_FORK, &sp);
#endif
}
}

//************************************
// ApplicationWayland
//************************************

ApplicationWayland::ApplicationWayland(int &argc, char **argv)
    : QApplication(argc, argv)
{
    app_init();
}

ApplicationWayland::~ApplicationWayland()
{
    if (!base->server) {
        return;
    }

    // need to unload all effects prior to destroying X connection as they might do X calls
    if (base->render->compositor->effects) {
        base->render->compositor->effects->unloadAllEffects();
    }

    if (exit_with_process && exit_with_process->state() != QProcess::NotRunning) {
        QObject::disconnect(exit_with_process, nullptr, this, nullptr);
        exit_with_process->terminate();
        exit_with_process->waitForFinished(5000);
        exit_with_process = nullptr;
    }

    // Kill Xwayland before terminating its connection.
    base->xwayland.reset();
    base->server->terminateClientConnections();

    if (base->render->compositor) {
        // Block compositor to prevent further compositing from crashing with a null workspace.
        // TODO(romangg): Instead we should kill the compositor before that or remove all outputs.
        base->render->compositor->lock();
    }

    base->space.reset();
    base->render->compositor.reset();
}

void ApplicationWayland::start(base::operation_mode mode,
                               std::string const& socket_name,
                               base::wayland::start_options flags,
                               QProcessEnvironment environment)
{
    assert(mode != base::operation_mode::x11);

    setQuitOnLastWindowClosed(false);

    using base_t = base::backend::wlroots::platform;
    base = std::make_unique<base_t>(base::config(KConfig::OpenFlag::FullConfig),
                                    socket_name,
                                    flags,
                                    base::backend::wlroots::start_options::none);
    base->operation_mode = mode;

    base->options = base::create_options(mode, base->config.main);

    auto session = new base::seat::backend::wlroots::session(base->wlroots_session, base->backend);
    base->session.reset(session);
    session->take_control(base->server->display->native());

    using render_t = render::backend::wlroots::platform<base_t>;
    base->render = std::make_unique<render_t>(*base);

    base->input = std::make_unique<input::backend::wlroots::platform>(
        *base, input::config(KConfig::NoGlobals));
    input::wayland::add_dbus(base->input.get());
    base->input->install_shortcuts();

    try {
        static_cast<render_t&>(*base->render).init();
    } catch (std::exception const&) {
        std::cerr << "FATAL ERROR: backend failed to initialize, exiting now" << std::endl;
        QCoreApplication::exit(1);
    }

    try {
        base->render->compositor = std::make_unique<render_t::compositor_t>(*base->render);
    } catch(std::system_error const& exc) {
        std::cerr << "FATAL ERROR: compositor creation failed: " << exc.what() << std::endl;
        exit(exc.code().value());
    }

    base->space = std::make_unique<base_t::space_t>(*base);
    win::init_shortcuts(*base->space);
    base->script = std::make_unique<scripting::platform<base_t::space_t>>(*base->space);

    base->render->compositor->start(*base->space);

    if (auto const& name = base->server->display->socket_name(); !name.empty()) {
        environment.insert(QStringLiteral("WAYLAND_DISPLAY"), name.c_str());
    }

    base->process_environment = environment;
    base->server->create_addons([this] { handle_server_addons_created(); });
}

void ApplicationWayland::handle_server_addons_created()
{
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
        base->xwayland = std::make_unique<xwl::xwayland<wayland_space>>(*base->space, status_callback);
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
            auto p = new QProcess(this);
            p->setProcessChannelMode(QProcess::ForwardedErrorChannel);
            p->setProcessEnvironment(process_environment);
            connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, p] (int code, QProcess::ExitStatus status) {
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
        for (const QString &application: qAsConst(m_applicationsToStart)) {
            QStringList arguments = KShell::splitArgs(application);
            if (arguments.isEmpty()) {
                qWarning("Failed to launch application: %s is an invalid command",
                         qPrintable(application));
                continue;
            }
            QString program = arguments.takeFirst();
            // note: this will kill the started process when we exit
            // this is going to happen anyway as we are the wayland and X server the app connects to
            auto p = new QProcess(this);
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
    QObject::connect(env_sync_job, &KUpdateLaunchEnvironmentJob::finished, this, []() {
        QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.KWinWrapper"));
    });
}

} // namespace

int main(int argc, char * argv[])
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

    KWin::app_setup_malloc();
    KWin::app_setup_localized_string();
    KWin::gainRealTime();

    signal(SIGPIPE, SIG_IGN);

    // ensure that no thread takes SIGUSR
    sigset_t userSignals;
    sigemptyset(&userSignals);
    sigaddset(&userSignals, SIGUSR1);
    sigaddset(&userSignals, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &userSignals, nullptr);

    // It's easy to exceed the file descriptor limit because many things are backed using fds
    // nowadays, e.g. dmabufs, shm buffers, etc. Bump the RLIMIT_NOFILE limit to handle that.
    // Some apps may still use select(), so we reset the limit to its original value in fork().
    if (KWin::bumpNofileLimit()) {
        pthread_atfork(nullptr, nullptr, KWin::restoreNofileLimit);
    }

    auto environment = QProcessEnvironment::systemEnvironment();

    // enforce our internal qpa plugin, unfortunately command line switch has precedence
    setenv("QT_QPA_PLATFORM", "wayland-org.kde.kwin.qpa", true);

    qunsetenv("QT_DEVICE_PIXEL_RATIO");
    qputenv("QSG_RENDER_LOOP", "basic");

    KWin::base::app_singleton app_singleton;
    KWin::ApplicationWayland a(argc, argv);

    // Reset QT_QPA_PLATFORM so we don't propagate it to our children (e.g. apps launched from the
    // overview effect).
    qunsetenv("QT_QPA_PLATFORM");

    KSignalHandler::self()->watchSignal(SIGTERM);
    KSignalHandler::self()->watchSignal(SIGINT);
    KSignalHandler::self()->watchSignal(SIGHUP);
    QObject::connect(KSignalHandler::self(), &KSignalHandler::signalReceived,
                     &a, &QCoreApplication::exit);

    KWin::app_create_about_data();
    QCommandLineOption xwaylandOption(QStringLiteral("xwayland"),
                                      i18n("Start a rootless Xwayland server."));
    QCommandLineOption waylandSocketOption(QStringList{QStringLiteral("s"), QStringLiteral("socket")},
                                           i18n("Name of the Wayland socket to listen on. If not set \"wayland-0\" is used."),
                                           QStringLiteral("socket"));

    QCommandLineParser parser;
    KWin::app_setup_command_line(&parser);

    parser.addOption(xwaylandOption);
    parser.addOption(waylandSocketOption);

    QCommandLineOption libinputOption(QStringLiteral("libinput"),
                                      i18n("Enable libinput support for input events processing. Note: never use in a nested session.	(deprecated)"));
    parser.addOption(libinputOption);

    QCommandLineOption screenLockerOption(QStringLiteral("lockscreen"),
                                          i18n("Starts the session in locked mode."));
    parser.addOption(screenLockerOption);

    QCommandLineOption noScreenLockerOption(QStringLiteral("no-lockscreen"),
                                            i18n("Starts the session without lock screen support."));
    parser.addOption(noScreenLockerOption);

    QCommandLineOption noGlobalShortcutsOption(QStringLiteral("no-global-shortcuts"),
                                               i18n("Starts the session without global shortcuts support."));
    parser.addOption(noGlobalShortcutsOption);

    QCommandLineOption exitWithSessionOption(QStringLiteral("exit-with-session"),
                                             i18n("Exit after the session application, which is started by KWin, closed."),
                                             QStringLiteral("/path/to/session"));
    parser.addOption(exitWithSessionOption);

    parser.addPositionalArgument(QStringLiteral("applications"),
                                 i18n("Applications to start once Wayland and Xwayland server are started"),
                                 QStringLiteral("[/path/to/application...]"));

    parser.process(a);
    KWin::app_process_command_line(a, &parser);

    if (parser.isSet(exitWithSessionOption)) {
        a.setSessionArgument(parser.value(exitWithSessionOption));
    }

    auto flags = KWin::base::wayland::start_options::none;
    if (parser.isSet(screenLockerOption)) {
        flags = KWin::base::wayland::start_options::lock_screen;
    } else if (parser.isSet(noScreenLockerOption)) {
        flags = KWin::base::wayland::start_options::no_lock_screen_integration;
    }
    if (parser.isSet(noGlobalShortcutsOption)) {
        flags |= KWin::base::wayland::start_options::no_global_shortcuts;
    }

    auto op_mode = parser.isSet(xwaylandOption) ? KWin::base::operation_mode::xwayland
                                                : KWin::base::operation_mode::wayland;

    a.setApplicationsToStart(parser.positionalArguments());
    a.start(op_mode, parser.value(waylandSocketOption).toStdString(), flags, environment);

    return a.exec();
}
