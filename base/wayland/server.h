/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "filtered_display.h"
#include "output_helpers.h"

#include "base/logging.h"
#include "input/types.h"
#include "kwinglobals.h"
#include "utils/flags.h"

#include <KScreenLocker/KsldApp>
#include <QObject>
#include <QThread>
#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/event_queue.h>
#include <Wrapland/Client/registry.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Server/client.h>
#include <Wrapland/Server/globals.h>
#include <memory>
#include <sys/socket.h>

namespace KWin::base::wayland
{

enum class start_options {
    none = 0x0,
    lock_screen = 0x1,
    no_lock_screen_integration = 0x2,
    no_global_shortcuts = 0x4,
};

}

ENUM_FLAGS(KWin::base::wayland::start_options)

namespace KWin::base::wayland
{

class KWIN_EXPORT server_qobject : public QObject
{
    Q_OBJECT
Q_SIGNALS:
    void internal_client_available();
    void terminating_internal_client_connection();
    void screenlocker_initialized();
};

template<typename Base>
class server
{
public:
    server(Base& base, std::string const& socket, start_options flags)
        : server(base, flags)
    {
        display->set_socket_name(socket);
        display->start(Wrapland::Server::Display::StartMode::ConnectToSocket);
        create_globals();
    }

    server(Base& base, int socket_fd, start_options flags)
        : server(base, flags)
    {
        display->add_socket_fd(socket_fd);
        display->start(Wrapland::Server::Display::StartMode::ConnectClientsOnly);
        create_globals();
    }

    void terminateClientConnections()
    {
        destroy_internal_connection();

        for (auto client : display->clients()) {
            client->destroy();
        }
    }

    Wrapland::Server::linux_dmabuf_v1* linux_dmabuf()
    {
        return globals->linux_dmabuf_v1.get();
    }

    Wrapland::Server::Viewporter* viewporter() const
    {
        return globals->viewporter.get();
    }

    Wrapland::Server::Seat* seat() const
    {
        if (globals->seats.empty()) {
            return nullptr;
        }
        return globals->seats.front().get();
    }

    Wrapland::Server::data_device_manager* data_device_manager() const
    {
        return globals->data_device_manager.get();
    }

    Wrapland::Server::primary_selection_device_manager* primary_selection_device_manager() const
    {
        return globals->primary_selection_device_manager.get();
    }

    /**
     * @returns file descriptor for Xwayland to connect to.
     */
    int create_xwayland_connection()
    {
        const auto socket = create_connection();
        if (!socket.connection) {
            return -1;
        }
        m_xwayland.client = socket.connection;
        m_xwayland.destroyConnection = QObject::connect(m_xwayland.client,
                                                        &Wrapland::Server::Client::disconnected,
                                                        qobject.get(),
                                                        [] { qFatal("Xwayland Connection died"); });
        return socket.fd;
    }

    void destroy_xwayland_connection()
    {
        if (!m_xwayland.client) {
            return;
        }
        QObject::disconnect(m_xwayland.destroyConnection);
        m_xwayland.client->destroy();
        m_xwayland.client = nullptr;
    }

    bool is_screen_locked() const
    {
        if (!has_screen_locker_integration()) {
            return false;
        }
        return ScreenLocker::KSldApp::self()->lockState() == ScreenLocker::KSldApp::Locked
            || ScreenLocker::KSldApp::self()->lockState() == ScreenLocker::KSldApp::AcquiringLock;
    }

    /**
     * @returns whether integration with KScreenLocker is available.
     */
    bool has_screen_locker_integration() const
    {
        return !(m_initFlags & start_options::no_lock_screen_integration);
    }

    /**
     * @returns whether any kind of global shortcuts are supported.
     */
    bool has_global_shortcut_support() const
    {
        return !(m_initFlags & start_options::no_global_shortcuts);
    }

    void create_addons(std::function<void()> callback)
    {
        auto handle_client_created = [this, callback](auto client_created) {
            if (client_created && has_screen_locker_integration()) {
                init_screen_locker();
            }
            callback();
        };
        create_internal_connection(handle_client_created);
    }

    Wrapland::Server::Client* xwayland_connection() const
    {
        return m_xwayland.client;
    }

    void dispatch()
    {
        if (!display) {
            return;
        }
        if (internal_connection.server) {
            internal_connection.server->flush();
        }
        display->dispatchEvents(0);
    }

    /**
     * Struct containing information for a created Wayland connection through a
     * socketpair.
     */
    struct socket_pair_connection {
        /**
         * ServerSide Connection
         */
        Wrapland::Server::Client* connection = nullptr;
        /**
         * client-side file descriptor for the socket
         */
        int fd = -1;
    };
    /**
     * Creates a Wayland connection using a socket pair.
     */
    socket_pair_connection create_connection()
    {
        socket_pair_connection ret;
        int sx[2];
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sx) < 0) {
            qCWarning(KWIN_CORE) << "Could not create socket";
            return ret;
        }
        ret.connection = display->createClient(sx[0]);
        ret.fd = sx[1];
        return ret;
    }

    void update_key_state(input::keyboard_leds leds)
    {
        if (!globals->key_state) {
            return;
        }

        using key = Wrapland::Server::KeyState::Key;
        using state = Wrapland::Server::KeyState::State;

        globals->key_state->setState(
            key::CapsLock,
            flags(leds & input::keyboard_leds::caps_lock) ? state::Locked : state::Unlocked);
        globals->key_state->setState(
            key::NumLock,
            flags(leds & input::keyboard_leds::num_lock) ? state::Locked : state::Unlocked);
        globals->key_state->setState(
            key::ScrollLock,
            flags(leds & input::keyboard_leds::scroll_lock) ? state::Locked : state::Unlocked);
    }

    std::unique_ptr<server_qobject> qobject;
    std::unique_ptr<Wrapland::Server::Display> display;
    std::unique_ptr<Wrapland::Server::globals> globals;

    struct {
        Wrapland::Server::Client* server{nullptr};
        Wrapland::Client::ConnectionThread* client{nullptr};
        QThread* clientThread{nullptr};
        Wrapland::Client::Registry* registry{nullptr};
        Wrapland::Client::Compositor* compositor{nullptr};
        Wrapland::Client::EventQueue* queue{nullptr};
        Wrapland::Client::Seat* seat{nullptr};
        Wrapland::Client::ShmPool* shm{nullptr};

    } internal_connection;

    Wrapland::Server::Client* screen_locker_client_connection{nullptr};

private:
    explicit server(Base& base, start_options flags)
        : qobject{std::make_unique<server_qobject>()}
        , display(std::make_unique<filtered_display>())
        , globals{std::make_unique<Wrapland::Server::globals>()}
        , m_initFlags{flags}
        , base{base}

    {
        qRegisterMetaType<Wrapland::Server::output_dpms_mode>();
    }

    void create_globals()
    {
        if (!display->running()) {
            qCCritical(KWIN_CORE) << "Wayland server failed to start.";
            throw std::exception();
        }

        display->createShm();
        globals->seats.push_back(display->createSeat());

        globals->pointer_gestures_v1 = display->createPointerGestures();
        globals->pointer_constraints_v1 = display->createPointerConstraints();
        globals->data_device_manager = display->createDataDeviceManager();
        globals->primary_selection_device_manager = display->createPrimarySelectionDeviceManager();
        globals->data_control_manager_v1 = display->create_data_control_manager_v1();

        globals->shadow_manager = display->createShadowManager();
        globals->dpms_manager = display->createDpmsManager();

        globals->output_management_v1 = display->createOutputManagementV1();
        QObject::connect(globals->output_management_v1.get(),
                         &Wrapland::Server::OutputManagementV1::configurationChangeRequested,
                         qobject.get(),
                         [this](auto config) { request_outputs_change(base, config); });

        globals->key_state = display->createKeyState();
        globals->viewporter = display->createViewporter();

        globals->relative_pointer_manager_v1 = display->createRelativePointerManager();
    }

    void create_internal_connection(std::function<void(bool)> callback)
    {
        const auto socket = create_connection();
        if (!socket.connection) {
            callback(false);
            return;
        }
        internal_connection.server = socket.connection;
        using namespace Wrapland::Client;
        internal_connection.client = new ConnectionThread();
        internal_connection.client->setSocketFd(socket.fd);
        internal_connection.clientThread = new QThread;
        internal_connection.client->moveToThread(internal_connection.clientThread);
        internal_connection.clientThread->start();

        QObject::connect(
            internal_connection.client,
            &ConnectionThread::establishedChanged,
            qobject.get(),
            [this, callback](bool established) {
                if (!established) {
                    return;
                }
                auto registry = new Registry;
                auto eventQueue = new EventQueue;
                eventQueue->setup(internal_connection.client);
                registry->setEventQueue(eventQueue);
                registry->create(internal_connection.client);
                internal_connection.registry = registry;
                internal_connection.queue = eventQueue;

                QObject::connect(
                    registry,
                    &Registry::interfacesAnnounced,
                    qobject.get(),
                    [this, callback, registry] {
                        auto create_interface
                            = [registry](Registry::Interface iface_code, auto creator) {
                                  auto iface = registry->interface(iface_code);
                                  assert(iface.name != 0);
                                  return (registry->*creator)(iface.name, iface.version, nullptr);
                              };

                        internal_connection.shm
                            = create_interface(Registry::Interface::Shm, &Registry::createShmPool);
                        internal_connection.compositor = create_interface(
                            Registry::Interface::Compositor, &Registry::createCompositor);
                        internal_connection.seat
                            = create_interface(Registry::Interface::Seat, &Registry::createSeat);
                        callback(true);
                        Q_EMIT qobject->internal_client_available();
                    },
                    Qt::QueuedConnection);

                registry->setup();
            });
        internal_connection.client->establishConnection();
    }

    int create_screen_locker_connection()
    {
        const auto socket = create_connection();
        if (!socket.connection) {
            return -1;
        }
        screen_locker_client_connection = socket.connection;
        QObject::connect(screen_locker_client_connection,
                         &Wrapland::Server::Client::disconnected,
                         qobject.get(),
                         [this] { screen_locker_client_connection = nullptr; });
        return socket.fd;
    }

    void destroy_internal_connection()
    {
        Q_EMIT qobject->terminating_internal_client_connection();
        if (internal_connection.client) {
            // delete all connections hold by plugins like e.g. widget style
            const auto connections = Wrapland::Client::ConnectionThread::connections();
            for (auto c : connections) {
                if (c == internal_connection.client) {
                    continue;
                }
                Q_EMIT c->establishedChanged(false);
            }

            delete internal_connection.registry;
            delete internal_connection.compositor;
            delete internal_connection.seat;
            delete internal_connection.shm;
            dispatch();
            delete internal_connection.queue;
            internal_connection.client->deleteLater();
            internal_connection.clientThread->quit();
            internal_connection.clientThread->wait();
            delete internal_connection.clientThread;
            internal_connection.client = nullptr;
            internal_connection.server->destroy();
            internal_connection.server = nullptr;
        }
    }

    void init_screen_locker()
    {
        if (!has_screen_locker_integration()) {
            return;
        }

        auto* screenLockerApp = ScreenLocker::KSldApp::self();

        ScreenLocker::KSldApp::self()->setGreeterEnvironment(base.process_environment);
        ScreenLocker::KSldApp::self()->initialize();

        QObject::connect(ScreenLocker::KSldApp::self(),
                         &ScreenLocker::KSldApp::aboutToLock,
                         qobject.get(),
                         [this, screenLockerApp]() {
                             if (screen_locker_client_connection) {
                                 // Already sent data to KScreenLocker.
                                 return;
                             }
                             int clientFd = create_screen_locker_connection();
                             if (clientFd < 0) {
                                 return;
                             }
                             ScreenLocker::KSldApp::self()->setWaylandFd(clientFd);

                             for (auto* seat : display->seats()) {
                                 QObject::connect(seat,
                                                  &Wrapland::Server::Seat::timestampChanged,
                                                  screenLockerApp,
                                                  &ScreenLocker::KSldApp::userActivity);
                             }
                         });

        QObject::connect(ScreenLocker::KSldApp::self(),
                         &ScreenLocker::KSldApp::unlocked,
                         qobject.get(),
                         [this, screenLockerApp]() {
                             if (screen_locker_client_connection) {
                                 screen_locker_client_connection->destroy();
                                 delete screen_locker_client_connection;
                                 screen_locker_client_connection = nullptr;
                             }

                             for (auto* seat : display->seats()) {
                                 QObject::disconnect(seat,
                                                     &Wrapland::Server::Seat::timestampChanged,
                                                     screenLockerApp,
                                                     &ScreenLocker::KSldApp::userActivity);
                             }
                             ScreenLocker::KSldApp::self()->setWaylandFd(-1);
                         });

        if (flags(m_initFlags & start_options::lock_screen)) {
            ScreenLocker::KSldApp::self()->lock(ScreenLocker::EstablishLock::Immediate);
        }

        Q_EMIT qobject->screenlocker_initialized();
    }

    struct {
        Wrapland::Server::Client* client = nullptr;
        QMetaObject::Connection destroyConnection;
    } m_xwayland;

    QHash<Wrapland::Server::Client*, quint16> m_clientIds;
    start_options m_initFlags;
    Base& base;
};

}
