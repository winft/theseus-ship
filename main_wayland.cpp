/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2014 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "main_wayland.h"
#include "workspace.h"
#include <config-kwin.h>

#include "base/backend/wlroots/platform.h"
#include "base/seat/backend/wlroots/session.h"
#include "debug/console/wayland/wayland_console.h"
#include "render/backend/wlroots/platform.h"
#include "render/effects.h"
#include "render/wayland/compositor.h"
#include "screenlockerwatcher.h"
#include "input/backend/wlroots/platform.h"
#include "input/wayland/cursor.h"
#include "input/wayland/platform.h"
#include "input/wayland/redirect.h"
#include "input/dbus/tablet_mode_manager.h"
#include "scripting/platform.h"
#include "wayland_server.h"
#include "win/wayland/space.h"
#include "xwl/xwayland.h"

// Wrapland
#include <Wrapland/Server/display.h>
#include <Wrapland/Server/seat.h>
// KDE
#include <KCrash>
#include <KLocalizedString>
#include <KPluginMetaData>
#include <KQuickAddons/QtQuickSettings>
#include <KShell>
#include <UpdateLaunchEnvironmentJob>

// Qt
#include <qplatformdefs.h>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QFileInfo>
#include <QProcess>
#include <QStyle>
#include <QDebug>
#include <QWindow>

// system
#if HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif
#if HAVE_SYS_PROCCTL_H
#include <sys/procctl.h>
#endif

#if HAVE_LIBCAP
#include <sys/capability.h>
#endif

#include <sched.h>

#include <iostream>
#include <iomanip>

Q_IMPORT_PLUGIN(KWinIntegrationPlugin)
Q_IMPORT_PLUGIN(KGlobalAccelImpl)
Q_IMPORT_PLUGIN(KWindowSystemKWinPlugin)
Q_IMPORT_PLUGIN(KWinIdleTimePoller)

namespace KWin
{

static void sighandler(int)
{
    QApplication::exit();
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
void gainRealTime(RealTimeFlags flags = RealTimeFlags::DontReset)
{
#if HAVE_SCHED_RESET_ON_FORK
    const int minPriority = sched_get_priority_min(SCHED_RR);
    struct sched_param sp;
    sp.sched_priority = minPriority;
    int policy = SCHED_RR;
    if (flags == RealTimeFlags::ResetOnFork) {
        policy |= SCHED_RESET_ON_FORK;
    }
    sched_setscheduler(0, policy, &sp);
#else
    Q_UNUSED(flags);
#endif
}
}

//************************************
// ApplicationWayland
//************************************

ApplicationWayland::ApplicationWayland(int &argc, char **argv)
    : Application(OperationModeWaylandOnly, argc, argv)
{
}

ApplicationWayland::~ApplicationWayland()
{
    setTerminating();
    if (!waylandServer()) {
        return;
    }

    // need to unload all effects prior to destroying X connection as they might do X calls
    if (effects) {
        static_cast<render::effects_handler_impl*>(effects)->unloadAllEffects();
    }

    if (exit_with_process && exit_with_process->state() != QProcess::NotRunning) {
        QObject::disconnect(exit_with_process, nullptr, this, nullptr);
        exit_with_process->terminate();
        exit_with_process->waitForFinished(5000);
        exit_with_process = nullptr;
    }

    // Kill Xwayland before terminating its connection.
    xwayland.reset();

    if (QStyle *s = style()) {
        // Unpolish style before terminating internal connection.
        s->unpolish(this);
    }

    waylandServer()->terminateClientConnections();

    if (base->render->compositor) {
        // Block compositor to prevent further compositing from crashing with a null workspace.
        // TODO(romangg): Instead we should kill the compositor before that or remove all outputs.
        static_cast<render::wayland::compositor*>(base->render->compositor.get())->lock();
    }

    workspace.reset();
    base->render->compositor.reset();
}

bool ApplicationWayland::is_screen_locked() const
{
    if (!server) {
        return false;
    }
    return server->is_screen_locked();
}

base::platform& ApplicationWayland::get_base()
{
    return *base;
}

WaylandServer* ApplicationWayland::get_wayland_server()
{
    return server.get();
}

debug::console* ApplicationWayland::create_debug_console()
{
    return new debug::wayland_console;
}

void ApplicationWayland::start()
{
    prepare_start();

    if (m_startXWayland) {
        setOperationMode(OperationModeXwayland);
    }

    base = std::make_unique<base::backend::wlroots::platform>(waylandServer()->display());

    base->render = std::make_unique<render::backend::wlroots::platform>(*base);
    auto render = static_cast<render::backend::wlroots::platform*>(base->render.get());

    createOptions();

    auto session = new base::seat::backend::wlroots::session(base->backend);
    this->session.reset(session);
    session->take_control();

    input.reset(new input::backend::wlroots::platform(*base));
    input::wayland::add_dbus(input.get());
    input->redirect->install_shortcuts();

    // now libinput thread has been created, adjust scheduler to not leak into other processes
    // TODO(romangg): can be removed?
    gainRealTime(RealTimeFlags::ResetOnFork);

    try {
        render->init();
    } catch (std::exception const&) {
        std::cerr << "FATAL ERROR: backend failed to initialize, exiting now" << std::endl;
        QCoreApplication::exit(1);
    }

    tablet_mode_manager = std::make_unique<input::dbus::tablet_mode_manager>();

    render->compositor = std::make_unique<render::wayland::compositor>(*render);
    workspace = std::make_unique<win::wayland::space>(server.get());
    Q_EMIT workspaceCreated();

    workspace->scripting = std::make_unique<scripting::platform>();

    waylandServer()->create_addons([this] { handle_server_addons_created(); });
    ScreenLockerWatcher::self()->initialize();
}

void ApplicationWayland::handle_server_addons_created()
{
    if (operationMode() == OperationModeXwayland) {
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
        xwayland.reset(new xwl::xwayland(this, status_callback));
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
    auto process_environment = processStartupEnvironment();

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
        for (const QString &application: m_applicationsToStart) {
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
    auto env_sync_job = new UpdateLaunchEnvironmentJob(process_environment);
    QObject::connect(env_sync_job, &UpdateLaunchEnvironmentJob::finished, this, []() {
        QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.KWinWrapper"));
    });

    Q_EMIT startup_finished();
}

static void disablePtrace()
{
#if HAVE_PR_SET_DUMPABLE
    // check whether we are running under a debugger
    const QFileInfo parent(QStringLiteral("/proc/%1/exe").arg(getppid()));
    if (parent.isSymLink() &&
            (parent.symLinkTarget().endsWith(QLatin1String("/gdb")) ||
             parent.symLinkTarget().endsWith(QLatin1String("/gdbserver")) ||
             parent.symLinkTarget().endsWith(QLatin1String("/lldb-server")))) {
        // debugger, don't adjust
        return;
    }

    // disable ptrace in kwin_wayland
    prctl(PR_SET_DUMPABLE, 0);
#endif
#if HAVE_PROC_TRACE_CTL
    // FreeBSD's rudimentary procfs does not support /proc/<pid>/exe
    // We could use the P_TRACED flag of the process to find out
    // if the process is being debugged ond FreeBSD.
    int mode = PROC_TRACE_CTL_DISABLE;
    procctl(P_PID, getpid(), PROC_TRACE_CTL, &mode);
#endif

}

static void unsetDumpable(int sig)
{
#if HAVE_PR_SET_DUMPABLE
    prctl(PR_SET_DUMPABLE, 1);
#endif
    signal(sig, SIG_IGN);
    raise(sig);
    return;
}

void dropNiceCapability()
{
#if HAVE_LIBCAP
    cap_t caps = cap_get_proc();
    if (!caps) {
        return;
    }
    cap_value_t capList[] = { CAP_SYS_NICE };
    if (cap_set_flag(caps, CAP_PERMITTED, 1, capList, CAP_CLEAR) == -1) {
        cap_free(caps);
        return;
    }
    if (cap_set_flag(caps, CAP_EFFECTIVE, 1, capList, CAP_CLEAR) == -1) {
        cap_free(caps);
        return;
    }
    cap_set_proc(caps);
    cap_free(caps);
#endif
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
    KWin::disablePtrace();
    KWin::Application::setupMalloc();
    KWin::Application::setupLocalizedString();
    KWin::gainRealTime();
    KWin::dropNiceCapability();

    if (signal(SIGTERM, KWin::sighandler) == SIG_IGN)
        signal(SIGTERM, SIG_IGN);
    if (signal(SIGINT, KWin::sighandler) == SIG_IGN)
        signal(SIGINT, SIG_IGN);
    if (signal(SIGHUP, KWin::sighandler) == SIG_IGN)
        signal(SIGHUP, SIG_IGN);
    signal(SIGABRT, KWin::unsetDumpable);
    signal(SIGSEGV, KWin::unsetDumpable);
    signal(SIGPIPE, SIG_IGN);
    // ensure that no thread takes SIGUSR
    sigset_t userSignals;
    sigemptyset(&userSignals);
    sigaddset(&userSignals, SIGUSR1);
    sigaddset(&userSignals, SIGUSR2);
    pthread_sigmask(SIG_BLOCK, &userSignals, nullptr);

    auto environment = QProcessEnvironment::systemEnvironment();

    // enforce our internal qpa plugin, unfortunately command line switch has precedence
    setenv("QT_QPA_PLATFORM", "wayland-org.kde.kwin.qpa", true);

    qunsetenv("QT_DEVICE_PIXEL_RATIO");
    qputenv("QSG_RENDER_LOOP", "basic");
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    KWin::ApplicationWayland a(argc, argv);
    a.setupTranslator();
    // reset QT_QPA_PLATFORM to a sane value for any processes started from KWin
    setenv("QT_QPA_PLATFORM", "wayland", true);

    KWin::Application::createAboutData();
    KQuickAddons::QtQuickSettings::init();

    QCommandLineOption xwaylandOption(QStringLiteral("xwayland"),
                                      i18n("Start a rootless Xwayland server."));
    QCommandLineOption waylandSocketOption(QStringList{QStringLiteral("s"), QStringLiteral("socket")},
                                           i18n("Name of the Wayland socket to listen on. If not set \"wayland-0\" is used."),
                                           QStringLiteral("socket"));

    QCommandLineParser parser;
    a.setupCommandLine(&parser);

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
    a.processCommandLine(&parser);

    if (parser.isSet(exitWithSessionOption)) {
        a.setSessionArgument(parser.value(exitWithSessionOption));
    }

    auto flags = KWin::wayland_start_options::none;
    if (parser.isSet(screenLockerOption)) {
        flags = KWin::wayland_start_options::lock_screen;
    } else if (parser.isSet(noScreenLockerOption)) {
        flags = KWin::wayland_start_options::no_lock_screen_integration;
    }
    if (parser.isSet(noGlobalShortcutsOption)) {
        flags |= KWin::wayland_start_options::no_global_shortcuts;
    }

    try {
        auto const socket_name = parser.value(waylandSocketOption).toStdString();
        a.server.reset(new KWin::WaylandServer(socket_name, flags));
    } catch (std::exception const&) {
        std::cerr << "FATAL ERROR: could not create Wayland server" << std::endl;
        return 1;
    }

    if (auto const& name = a.server->display()->socket_name(); !name.empty()) {
        environment.insert(QStringLiteral("WAYLAND_DISPLAY"), name.c_str());
    }

    a.setProcessStartupEnvironment(environment);
    a.setStartXwayland(parser.isSet(xwaylandOption));
    a.setApplicationsToStart(parser.positionalArguments());
    a.start();

    return a.exec();
}
