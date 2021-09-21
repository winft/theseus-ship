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
// kwin
#include "platform.h"
#include "effects.h"
#include "render/wayland/compositor.h"
#include "seat/backend/logind/session.h"
#include "seat/backend/wlroots/session.h"
#include "input/backend/wlroots/platform.h"
#include "input/wayland/cursor.h"
#include "input/dbus/tablet_mode_manager.h"
#include "wayland_server.h"
#include "xwl/xwayland.h"

// Wrapland
#include <Wrapland/Server/display.h>
#include <Wrapland/Server/seat.h>
// KDE
#include <KCrash>
#include <KLocalizedString>
#include <KPluginLoader>
#include <KPluginMetaData>
#include <KQuickAddons/QtQuickSettings>
#include <KShell>

// Qt
#include <qplatformdefs.h>
#include <QCommandLineParser>
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
    : ApplicationWaylandAbstract(OperationModeWaylandOnly, argc, argv)
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
        static_cast<EffectsHandlerImpl*>(effects)->unloadAllEffects();
    }

    if (exit_with_process && exit_with_process->state() != QProcess::NotRunning) {
        QObject::disconnect(exit_with_process, nullptr, this, nullptr);
        exit_with_process->terminate();
        exit_with_process->waitForFinished(5000);
        exit_with_process = nullptr;
    }

    // Kill Xwayland before terminating its connection.
    delete m_xwayland;
    m_xwayland = nullptr;

    if (QStyle *s = style()) {
        // Unpolish style before terminating internal connection.
        s->unpolish(this);
    }

    waylandServer()->terminateClientConnections();

    if (compositor) {
        // Block compositor to prevent further compositing from crashing with a null workspace.
        // TODO(romangg): Instead we should kill the compositor before that or remove all outputs.
        static_cast<render::wayland::compositor*>(compositor)->lock();
    }
    destroyWorkspace();

    destroyCompositor();
}

void ApplicationWayland::performStartup()
{
    if (m_startXWayland) {
        setOperationMode(OperationModeXwayland);
    }

    createOptions();
    waylandServer()->createInternalConnection();

    auto session = new seat::backend::wlroots::session(backend->backend);
    this->session.reset(session);
    session->take_control();
    input::add_redirect(input.get());
    input->cursor.reset(new input::wayland::cursor);

    // now libinput thread has been created, adjust scheduler to not leak into other processes
    // TODO(romangg): can be removed?
    gainRealTime(RealTimeFlags::ResetOnFork);

    input->redirect->set_platform(input.get());

    try {
        render->init();
    } catch (std::exception const&) {
        std::cerr << "FATAL ERROR: backend failed to initialize, exiting now" << std::endl;
        QCoreApplication::exit(1);
    }

    input::dbus::tablet_mode_manager::create(this);
}

void ApplicationWayland::continueStartupWithCompositor()
{
    render::wayland::compositor::create();

    if (operationMode() == OperationModeXwayland) {
        create_xwayland();
    } else {
        init_workspace();
    }
}

void ApplicationWayland::init_platforms()
{
    backend.reset(new platform_base::wlroots(waylandServer()->display()));

    input.reset(new input::backend::wlroots::platform(backend.get()));
    input::add_dbus(input.get());

    render.reset(new render::backend::wlroots::backend(backend.get(), this));
    platform = render.get();
}

void ApplicationWayland::init_workspace()
{
    if (m_xwayland) {
        disconnect(m_xwayland, &Xwl::Xwayland::initialized, this, &ApplicationWayland::init_workspace);
    }
    startSession();
    createWorkspace();
    waylandServer()->initWorkspace();

    Q_EMIT startup_finished();
}

void ApplicationWayland::create_xwayland()
{
    m_xwayland = new Xwl::Xwayland(this);
    connect(m_xwayland, &Xwl::Xwayland::criticalError, this, [](int code) {
        // we currently exit on Xwayland errors always directly
        // TODO: restart Xwayland
        std::cerr << "Xwayland had a critical error. Going to exit now." << std::endl;
        exit(code);
    });
    connect(m_xwayland, &Xwl::Xwayland::initialized, this, &ApplicationWayland::init_workspace);
    m_xwayland->init();
}

void ApplicationWayland::startSession()
{
    auto process_environment = processStartupEnvironment();

    // Enforce Wayland platform for started Qt apps. They otherwise for some reason prefer X11.
    process_environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("wayland"));

    if (!m_inputMethodServerToStart.isEmpty()) {
        QStringList arguments = KShell::splitArgs(m_inputMethodServerToStart);
        if (!arguments.isEmpty()) {
            QString program = arguments.takeFirst();
            int socket = dup(waylandServer()->createInputMethodConnection());
            if (socket >= 0) {
                auto environment = process_environment;
                environment.insert(QStringLiteral("WAYLAND_SOCKET"), QByteArray::number(socket));
                environment.remove("DISPLAY");
                environment.remove("WAYLAND_DISPLAY");
                QProcess *p = new Process(this);
                p->setProcessChannelMode(QProcess::ForwardedErrorChannel);
                connect(p, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                    [p] {
                        if (waylandServer()) {
                            waylandServer()->destroyInputMethodConnection();
                        }
                        p->deleteLater();
                    }
                );
                p->setProcessEnvironment(environment);
                p->setProgram(program);
                p->setArguments(arguments);
                p->start();
                p->waitForStarted(); //do we really need to wait?
            }
        } else {
            qWarning("Failed to launch the input method server: %s is an invalid command",
                     qPrintable(m_inputMethodServerToStart));
        }
    }

    // start session
    if (!m_sessionArgument.isEmpty()) {
        QStringList arguments = KShell::splitArgs(m_sessionArgument);
        if (!arguments.isEmpty()) {
            QString program = arguments.takeFirst();
            QProcess *p = new Process(this);
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
            QProcess *p = new Process(this);
            p->setProcessChannelMode(QProcess::ForwardedErrorChannel);
            p->setProcessEnvironment(process_environment);
            p->setProgram(program);
            p->setArguments(arguments);
            p->startDetached();
            p->deleteLater();
        }
    }
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
    QCommandLineOption waylandDisplayOption(QStringLiteral("wayland-display"),
                                            i18n("The Wayland Display to use in windowed mode on platform Wayland."),
                                            QStringLiteral("display"));
    QCommandLineOption wayland_socket_fd_option(QStringLiteral("wayland_fd"),
                                    i18n("Wayland socket to use for incoming connections."),
                                    QStringLiteral("wayland_fd"));

    QCommandLineParser parser;
    a.setupCommandLine(&parser);

    parser.addOption(xwaylandOption);
    parser.addOption(waylandSocketOption);
    parser.addOption(wayland_socket_fd_option);
    parser.addOption(waylandDisplayOption);

    QCommandLineOption libinputOption(QStringLiteral("libinput"),
                                      i18n("Enable libinput support for input events processing. Note: never use in a nested session.	(deprecated)"));
    parser.addOption(libinputOption);

    QCommandLineOption inputMethodOption(QStringLiteral("inputmethod"),
                                         i18n("Input method that KWin starts."),
                                         QStringLiteral("path/to/imserver"));
    parser.addOption(inputMethodOption);

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

#ifdef KWIN_BUILD_ACTIVITIES
    a.setUseKActivities(false);
#endif

    if (parser.isSet(exitWithSessionOption)) {
        a.setSessionArgument(parser.value(exitWithSessionOption));
    }

    QSize initialWindowSize;
    QByteArray deviceIdentifier;
    qreal outputScale = 1;

    if (parser.isSet(waylandDisplayOption)) {
        deviceIdentifier = parser.value(waylandDisplayOption).toUtf8();
    }

    auto const wrapped_process = parser.isSet(wayland_socket_fd_option);

    KWin::WaylandServer::InitializationFlags flags;
    if (parser.isSet(screenLockerOption)) {
        flags = KWin::WaylandServer::InitializationFlag::LockScreen;
    } else if (parser.isSet(noScreenLockerOption)) {
        flags = KWin::WaylandServer::InitializationFlag::NoLockScreenIntegration;
    }
    if (parser.isSet(noGlobalShortcutsOption)) {
        flags |= KWin::WaylandServer::InitializationFlag::NoGlobalShortcuts;
    }

    try {
        if (parser.isSet(wayland_socket_fd_option)) {
            bool ok;
            auto fd = parser.value(wayland_socket_fd_option).toInt(&ok);

            if (!ok) {
                std::cerr << "FATAL ERROR: could not parse socket fd" << std::endl;
                throw std::exception();
            }

            // Ensure fd is not leaked to children.
            fcntl(fd, F_SETFD, O_CLOEXEC);
            a.server.reset(new KWin::WaylandServer(fd, flags));
        } else {
            auto const socket_name = parser.value(waylandSocketOption).toStdString();
            a.server.reset(new KWin::WaylandServer(socket_name, flags));
        }
    } catch (std::exception const&) {
        std::cerr << "FATAL ERROR: could not create Wayland server" << std::endl;
        return 1;
    }


    if (wrapped_process) {
        // If we run with the wrapper, we must temporarily unset the WAYLAND_DISPLAY environment
        // variable for the wlroots backend initialization. Otherwise wlroots would select its
        // nested Wayland backend.
        assert(qEnvironmentVariableIsSet("WAYLAND_DISPLAY"));
        auto const display_to_use = qgetenv("WAYLAND_DISPLAY");
        qunsetenv("WAYLAND_DISPLAY");

        if (parser.isSet(waylandDisplayOption)) {
            // If we are indeed in a nested Wayland session set WAYLAND_DISPLAY to the host
            // session's one, so wlroots does select its Wayland backend.
            qputenv("WAYLAND_DISPLAY", parser.value(waylandDisplayOption).toUtf8());
        }

        a.init_platforms();
        qputenv("WAYLAND_DISPLAY", display_to_use);
    } else {
        a.init_platforms();
    }

    if (!deviceIdentifier.isEmpty()) {
        a.platform->setDeviceIdentifier(deviceIdentifier);
    }
    if (initialWindowSize.isValid()) {
        a.platform->setInitialWindowSize(initialWindowSize);
    }
    a.platform->setInitialOutputScale(outputScale);

    if (auto const& name = a.server->display()->socketName(); !name.empty()) {
        environment.insert(QStringLiteral("WAYLAND_DISPLAY"), name.c_str());
    }

    a.setProcessStartupEnvironment(environment);
    a.setStartXwayland(parser.isSet(xwaylandOption));
    a.setApplicationsToStart(parser.positionalArguments());
    a.setInputMethodServerToStart(parser.value(inputMethodOption));
    a.start();

    return a.exec();
}
