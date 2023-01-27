/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/types.h"
#include "kwinglobals.h"
#include "utils/flags.h"

#include <QObject>
#include <QSet>
#include <memory>

class QThread;

namespace Wrapland
{
namespace Client
{
class Compositor;
class ConnectionThread;
class EventQueue;
class Registry;
class Seat;
class ShmPool;
}
namespace Server
{
class Display;
struct globals;

class Client;
class Compositor;
class data_device_manager;
class drm_lease_device_v1;
class KdeIdle;
class KeyState;
class LayerShellV1;
class linux_dmabuf_v1;
class PlasmaVirtualDesktopManager;
class PlasmaWindowManager;
class PresentationManager;
class primary_selection_device_manager;
class Seat;
class Subcompositor;
class Surface;
class Viewporter;
class XdgActivationV1;
class XdgShell;
}
}

namespace KWin::base::wayland
{

enum class start_options {
    none = 0x0,
    lock_screen = 0x1,
    no_lock_screen_integration = 0x2,
    no_global_shortcuts = 0x4,
};

class KWIN_EXPORT server_qobject : public QObject
{
    Q_OBJECT

Q_SIGNALS:
    void internal_client_available();
    void terminating_internal_client_connection();
    void screenlocker_initialized();
};

class KWIN_EXPORT server
{
public:
    server(std::string const& socket, start_options flags);
    server(int socket_fd, start_options flags);
    ~server();

    void terminateClientConnections();

    Wrapland::Server::linux_dmabuf_v1* linux_dmabuf();
    Wrapland::Server::Viewporter* viewporter() const;

    Wrapland::Server::Seat* seat() const;

    Wrapland::Server::data_device_manager* data_device_manager() const;
    Wrapland::Server::primary_selection_device_manager* primary_selection_device_manager() const;

    /**
     * @returns file descriptor for Xwayland to connect to.
     */
    int create_xwayland_connection();
    void destroy_xwayland_connection();

    bool is_screen_locked() const;
    /**
     * @returns whether integration with KScreenLocker is available.
     */
    bool has_screen_locker_integration() const;

    /**
     * @returns whether any kind of global shortcuts are supported.
     */
    bool has_global_shortcut_support() const;

    void create_addons(std::function<void()> callback);

    Wrapland::Server::Client* xwayland_connection() const
    {
        return m_xwayland.client;
    }

    void dispatch();

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
    socket_pair_connection create_connection();

    void update_key_state(input::keyboard_leds leds);

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
    explicit server(start_options flags);

    void create_globals();
    void create_internal_connection(std::function<void(bool)> callback);
    int create_screen_locker_connection();

    void destroy_internal_connection();
    void init_screen_locker();

    struct {
        Wrapland::Server::Client* client = nullptr;
        QMetaObject::Connection destroyConnection;
    } m_xwayland;

    QHash<Wrapland::Server::Client*, quint16> m_clientIds;
    start_options m_initFlags;
};

}

ENUM_FLAGS(KWin::base::wayland::start_options)
