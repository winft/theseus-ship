/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2014 Martin Gräßlin <mgraesslin@kde.org>
Copyright 2019 Roman Gilg <subdiff@gmail.com>

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
#include "xwayland.h"
#include "data_bridge.h"

#include "base/x11/xcb_event_filter.h"
#include "input/cursor.h"
#include "main_wayland.h"
#include "utils.h"
#include "wayland_server.h"
#include "win/wayland/space.h"
#include "win/x11/space_setup.h"
#include "workspace.h"
#include "xcbutils.h"

#include <KLocalizedString>
#include <KSelectionOwner>

#include <QAbstractEventDispatcher>
#include <QFile>
#include <QFutureWatcher>
#include <QProcess>
#include <QSocketNotifier>
#include <QThread>
#include <QtConcurrentRun>

// system
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_SYS_PROCCTL_H
#include <unistd.h>
#endif

#include <iostream>
#include <sys/socket.h>

static void readDisplay(int pipe)
{
    QFile readPipe;
    if (!readPipe.open(pipe, QIODevice::ReadOnly)) {
        std::cerr << "FATAL ERROR failed to open pipe to start X Server" << std::endl;
        exit(1);
    }
    auto displayNumber = readPipe.readLine();

    displayNumber.prepend(QByteArray(":"));
    displayNumber.remove(displayNumber.size() - 1, 1);
    std::cout << "X-Server started on display " << displayNumber.constData() << std::endl;

    setenv("DISPLAY", displayNumber.constData(), true);

    // close our pipe
    close(pipe);
}

namespace KWin::xwl
{

xwayland::xwayland(Application* app, std::function<void(int)> status_callback)
    : xwayland_interface()
    , app{app}
    , status_callback{status_callback}
{
    int pipeFds[2];
    if (pipe(pipeFds) != 0) {
        throw std::runtime_error("Failed to create pipe to start Xwayland");
    }

    int sx[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sx) < 0) {
        throw std::runtime_error("Failed to open socket to open XCB connection");
    }

    int fd = dup(sx[1]);
    if (fd < 0) {
        throw std::system_error(std::error_code(20, std::generic_category()),
                                "Failed to dup socket to open XCB connection");
    }

    auto const waylandSocket = waylandServer()->createXWaylandConnection();
    if (waylandSocket == -1) {
        close(fd);
        throw std::runtime_error("Failed to open socket for Xwayland");
    }
    auto const wlfd = dup(waylandSocket);
    if (wlfd < 0) {
        close(fd);
        throw std::system_error(std::error_code(20, std::generic_category()),
                                "Failed to dup socket for Xwayland");
    }

    xcb_connection_fd = sx[0];

    xwayland_process = new QProcess(this);
    xwayland_process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    xwayland_process->setProgram(QStringLiteral("Xwayland"));

    QProcessEnvironment env = app->processStartupEnvironment();
    env.insert("WAYLAND_SOCKET", QByteArray::number(wlfd));
    env.insert("EGL_PLATFORM", QByteArrayLiteral("DRM"));

    if (qEnvironmentVariableIsSet("KWIN_XWAYLAND_DEBUG")) {
        env.insert("WAYLAND_DEBUG", QByteArrayLiteral("1"));
    }

    xwayland_process->setProcessEnvironment(env);
    xwayland_process->setArguments({QStringLiteral("-displayfd"),
                                    QString::number(pipeFds[1]),
                                    QStringLiteral("-rootless"),
                                    QStringLiteral("-wm"),
                                    QString::number(fd)});

    xwayland_fail_notifier = connect(
        xwayland_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                std::cerr << "FATAL ERROR: failed to start Xwayland" << std::endl;
            } else {
                std::cerr << "FATAL ERROR: Xwayland failed, going to exit now" << std::endl;
            }
            this->status_callback(1);
        });

    auto const xDisplayPipe = pipeFds[0];
    connect(xwayland_process, &QProcess::started, this, [this, xDisplayPipe] {
        QFutureWatcher<void>* watcher = new QFutureWatcher<void>(this);
        QObject::connect(watcher,
                         &QFutureWatcher<void>::finished,
                         this,
                         &xwayland::continue_startup_with_x11,
                         Qt::QueuedConnection);
        QObject::connect(watcher,
                         &QFutureWatcher<void>::finished,
                         watcher,
                         &QFutureWatcher<void>::deleteLater,
                         Qt::QueuedConnection);
        watcher->setFuture(QtConcurrent::run(readDisplay, xDisplayPipe));
    });

    xwayland_process->start();
    close(pipeFds[1]);
}

xwayland::~xwayland()
{
    data_bridge.reset();

    disconnect(xwayland_fail_notifier);

    win::x11::clear_space(*Workspace::self());

    if (app->x11Connection()) {
        Xcb::setInputFocus(XCB_INPUT_FOCUS_POINTER_ROOT);
        Workspace::self()->atoms.reset();
        Q_EMIT app->x11ConnectionAboutToBeDestroyed();
        app->setX11Connection(nullptr);
        xcb_disconnect(app->x11Connection());
    }

    if (xwayland_process->state() != QProcess::NotRunning) {
        disconnect(xwayland_process, nullptr, this, nullptr);
        xwayland_process->terminate();
        xwayland_process->waitForFinished(5000);
    }

    delete xwayland_process;
    xwayland_process = nullptr;

    waylandServer()->destroyXWaylandConnection();
}

void xwayland::continue_startup_with_x11()
{
    auto screenNumber = 0;

    if (xcb_connection_fd == -1) {
        basic_data.connection = xcb_connect(nullptr, &screenNumber);
    } else {
        basic_data.connection = xcb_connect_to_fd(xcb_connection_fd, nullptr);
    }

    if (int error = xcb_connection_has_error(basic_data.connection)) {
        std::cerr << "FATAL ERROR connecting to Xwayland server: " << error << std::endl;
        status_callback(1);
        return;
    }

    auto iter = xcb_setup_roots_iterator(xcb_get_setup(basic_data.connection));
    basic_data.screen = iter.data;
    assert(basic_data.screen);

    app->setX11Connection(basic_data.connection, false);

    // we don't support X11 multi-head in Wayland
    app->setX11ScreenNumber(screenNumber);
    app->setX11RootWindow(defaultScreen()->root);

    xcb_read_notifier.reset(
        new QSocketNotifier(xcb_get_file_descriptor(basic_data.connection), QSocketNotifier::Read));

    auto processXcbEvents = [this] {
        while (auto event = xcb_poll_for_event(basic_data.connection)) {
            if (data_bridge->filter_event(event)) {
                free(event);
                continue;
            }
            long result = 0;
            QThread::currentThread()->eventDispatcher()->filterNativeEvent(
                QByteArrayLiteral("xcb_generic_event_t"), event, &result);
            free(event);
        }
        xcb_flush(basic_data.connection);
    };

    connect(xcb_read_notifier.get(), &QSocketNotifier::activated, this, processXcbEvents);
    connect(QThread::currentThread()->eventDispatcher(),
            &QAbstractEventDispatcher::aboutToBlock,
            this,
            processXcbEvents);
    connect(QThread::currentThread()->eventDispatcher(),
            &QAbstractEventDispatcher::awake,
            this,
            processXcbEvents);

    // create selection owner for WM_S0 - magic X display number expected by XWayland
    KSelectionOwner owner("WM_S0", basic_data.connection, app->x11RootWindow());
    owner.claim(true);

    auto space = static_cast<win::wayland::space*>(Workspace::self());
    space->atoms = std::make_unique<Atoms>(basic_data.connection);
    basic_data.atoms = space->atoms.get();

    event_filter = std::make_unique<base::x11::xcb_event_filter<win::wayland::space>>(*space);
    app->installNativeEventFilter(event_filter.get());

    // Check  whether another windowmanager is running
    uint32_t const maskValues[] = {XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT};
    ScopedCPointer<xcb_generic_error_t> redirectCheck(
        xcb_request_check(connection(),
                          xcb_change_window_attributes_checked(
                              connection(), rootWindow(), XCB_CW_EVENT_MASK, maskValues)));
    if (!redirectCheck.isNull()) {
        fputs(i18n("kwin_wayland: an X11 window manager is running on the X11 Display.\n")
                  .toLocal8Bit()
                  .constData(),
              stderr);
        status_callback(1);
        return;
    }

    auto mouseCursor = input::get_cursor();
    if (mouseCursor) {
        Xcb::defineCursor(app->x11RootWindow(), mouseCursor->x11_cursor(Qt::ArrowCursor));
    }

    auto env = app->processStartupEnvironment();
    env.insert(QStringLiteral("DISPLAY"), QString::fromUtf8(qgetenv("DISPLAY")));
    app->setProcessStartupEnvironment(env);

    status_callback(0);
    win::x11::init_space(*static_cast<win::wayland::space*>(Workspace::self()));
    Q_EMIT app->x11ConnectionChanged();

    // Trigger possible errors, there's still a chance to abort
    Xcb::sync();

    data_bridge.reset(new xwl::data_bridge(basic_data));
}

drag_event_reply xwayland::drag_move_filter(Toplevel* target, QPoint const& pos)
{
    if (!data_bridge) {
        return drag_event_reply::wayland;
    }
    return data_bridge->drag_move_filter(target, pos);
}

}
