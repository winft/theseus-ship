/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

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
#pragma once

#include "data_bridge.h"
#include "types.h"

#include "base/wayland/server.h"
#include "base/x11/xcb/helpers.h"
#include "base/x11/xcb_event_filter.h"
#include "input/cursor.h"
#include "main.h"
#include "win/wayland/surface.h"
#include "win/wayland/xwl_window.h"
#include "win/x11/space_setup.h"

#include <KLocalizedString>
#include <KSelectionOwner>
#include <QAbstractEventDispatcher>
#include <QFile>
#include <QFutureWatcher>
#include <QProcess>
#include <QSocketNotifier>
#include <QThread>
#include <QtConcurrentRun>

#include <iostream>
#include <sys/socket.h>

#include <memory>
#include <xcb/xproto.h>

namespace KWin::xwl
{

inline void read_display(int pipe)
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

template<typename Space>
class xwayland : public xwayland_interface
{
public:
    /** The @ref status_callback is called once with 0 code when Xwayland is ready, other codes
     *  indicate a critical error happened at runtime.
     */
    xwayland(Application* app, Space& space, std::function<void(int code)> status_callback)
        : core{&space}
        , app{app}
        , space{space}
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

        auto const waylandSocket = waylandServer()->create_xwayland_connection();
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
            watcher->setFuture(QtConcurrent::run(read_display, xDisplayPipe));
        });

        xwayland_process->start();
        close(pipeFds[1]);
    }

    ~xwayland() override
    {
        data_bridge.reset();

        disconnect(xwayland_fail_notifier);

        win::x11::clear_space(space);

        if (app->x11Connection()) {
            base::x11::xcb::set_input_focus(XCB_INPUT_FOCUS_POINTER_ROOT);
            space.atoms.reset();
            core.x11.atoms = nullptr;
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

        waylandServer()->destroy_xwayland_connection();
    }

    std::unique_ptr<xwl::data_bridge<Toplevel>> data_bridge;

private:
    void continue_startup_with_x11()
    {
        auto screenNumber = 0;

        if (xcb_connection_fd == -1) {
            core.x11.connection = xcb_connect(nullptr, &screenNumber);
        } else {
            core.x11.connection = xcb_connect_to_fd(xcb_connection_fd, nullptr);
        }

        if (int error = xcb_connection_has_error(core.x11.connection)) {
            std::cerr << "FATAL ERROR connecting to Xwayland server: " << error << std::endl;
            status_callback(1);
            return;
        }

        auto iter = xcb_setup_roots_iterator(xcb_get_setup(core.x11.connection));
        core.x11.screen = iter.data;
        assert(core.x11.screen);

        app->setX11Connection(core.x11.connection, false);

        // we don't support X11 multi-head in Wayland
        app->setX11ScreenNumber(screenNumber);
        app->setX11RootWindow(defaultScreen()->root);

        xcb_read_notifier.reset(new QSocketNotifier(xcb_get_file_descriptor(core.x11.connection),
                                                    QSocketNotifier::Read));

        auto processXcbEvents = [this] {
            while (auto event = xcb_poll_for_event(core.x11.connection)) {
                if (data_bridge->filter_event(event)) {
                    free(event);
                    continue;
                }
                long result = 0;
                QThread::currentThread()->eventDispatcher()->filterNativeEvent(
                    QByteArrayLiteral("xcb_generic_event_t"), event, &result);
                free(event);
            }
            xcb_flush(core.x11.connection);
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
        KSelectionOwner owner("WM_S0", core.x11.connection, app->x11RootWindow());
        owner.claim(true);

        space.atoms = std::make_unique<base::x11::atoms>(core.x11.connection);
        core.x11.atoms = space.atoms.get();
        event_filter = std::make_unique<base::x11::xcb_event_filter<Space>>(space);
        app->installNativeEventFilter(event_filter.get());

        QObject::connect(
            space.qobject.get(),
            &win::space::qobject_t::surface_id_changed,
            this,
            [this, xwayland_connection = waylandServer()->xwayland_connection()](auto win_id,
                                                                                 auto id) {
                if (auto surface = space.compositor->getSurface(id, xwayland_connection)) {
                    auto win = space.windows_map.at(win_id);
                    auto xwl_win = dynamic_cast<win::wayland::xwl_window<Space>*>(win);
                    assert(xwl_win);
                    win::wayland::set_surface(xwl_win, surface);
                }
            });

        // Check  whether another windowmanager is running
        uint32_t const maskValues[] = {XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT};
        unique_cptr<xcb_generic_error_t> redirectCheck(
            xcb_request_check(connection(),
                              xcb_change_window_attributes_checked(
                                  connection(), rootWindow(), XCB_CW_EVENT_MASK, maskValues)));

        if (redirectCheck) {
            fputs(i18n("kwin_wayland: an X11 window manager is running on the X11 Display.\n")
                      .toLocal8Bit()
                      .constData(),
                  stderr);
            status_callback(1);
            return;
        }

        if (auto& cursor = space.input->platform.cursor) {
            base::x11::xcb::define_cursor(app->x11RootWindow(),
                                          cursor->x11_cursor(Qt::ArrowCursor));
        }

        auto env = app->processStartupEnvironment();
        env.insert(QStringLiteral("DISPLAY"), QString::fromUtf8(qgetenv("DISPLAY")));
        app->setProcessStartupEnvironment(env);

        status_callback(0);
        win::x11::init_space(space);
        Q_EMIT app->x11ConnectionChanged();

        // Trigger possible errors, there's still a chance to abort
        base::x11::xcb::sync();

        data_bridge = std::make_unique<xwl::data_bridge<Toplevel>>(core);
    }

    drag_event_reply drag_move_filter(Toplevel* target, QPoint const& pos) override
    {
        if (!data_bridge) {
            return drag_event_reply::wayland;
        }
        return data_bridge->drag_move_filter(target, pos);
    }

    int xcb_connection_fd{-1};
    QProcess* xwayland_process{nullptr};
    QMetaObject::Connection xwayland_fail_notifier;

    runtime<win::space> core;

    std::unique_ptr<QSocketNotifier> xcb_read_notifier;
    std::unique_ptr<base::x11::xcb_event_filter<Space>> event_filter;

    Application* app;
    Space& space;
    std::function<void(int code)> status_callback;
};

}
