/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "input/xkb.h"

#include <kwinglobals.h>

#include <QObject>

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
class Surface;
}
namespace Server
{
class Display;
struct globals;

class AppmenuManager;
class Client;
class Compositor;
class data_device_manager;
class drm_lease_device_v1;
class KdeIdle;
class KeyState;
class LayerShellV1;
class LinuxDmabufBufferV1;
class LinuxDmabufV1;
class Output;
class OutputConfigurationV1;
class OutputManagementV1;
class PlasmaShell;
class PlasmaShellSurface;
class PlasmaVirtualDesktopManager;
class PlasmaWindowManager;
class PresentationManager;
class primary_selection_device_manager;
class QtSurfaceExtension;
class Seat;
class ServerSideDecorationPaletteManager;
class Subcompositor;
class Surface;
class Viewporter;
class XdgActivationV1;
class XdgDecorationManager;
class XdgForeign;
class XdgShell;
}
}

namespace KWin
{

namespace win::wayland
{
class window;
}

class Toplevel;

class KWIN_EXPORT WaylandServer : public QObject
{
    Q_OBJECT
public:
    static WaylandServer* self();

    enum class InitializationFlag {
        NoOptions = 0x0,
        LockScreen = 0x1,
        NoLockScreenIntegration = 0x2,
        NoGlobalShortcuts = 0x4,
    };

    Q_DECLARE_FLAGS(InitializationFlags, InitializationFlag)

    std::vector<win::wayland::window*> windows;
    QVector<Wrapland::Server::PlasmaShellSurface*> m_plasmaShellSurfaces;

    WaylandServer(std::string const& socket, InitializationFlags flags);
    WaylandServer(int socket_fd, InitializationFlags flags);
    ~WaylandServer() override;

    void terminateClientConnections();

    Wrapland::Server::Display* display() const;

    Wrapland::Server::Compositor* compositor() const;
    Wrapland::Server::Subcompositor* subcompositor() const;
    Wrapland::Server::LinuxDmabufV1* linux_dmabuf();
    Wrapland::Server::Viewporter* viewporter() const;
    Wrapland::Server::PresentationManager* presentation_manager() const;

    Wrapland::Server::Seat* seat() const;

    Wrapland::Server::data_device_manager* data_device_manager() const;
    Wrapland::Server::primary_selection_device_manager* primary_selection_device_manager() const;

    Wrapland::Server::XdgShell* xdg_shell() const;
    Wrapland::Server::XdgActivationV1* xdg_activation() const;

    Wrapland::Server::PlasmaVirtualDesktopManager* virtual_desktop_management() const;
    Wrapland::Server::LayerShellV1* layer_shell() const;
    Wrapland::Server::PlasmaWindowManager* window_management() const;

    Wrapland::Server::KdeIdle* kde_idle() const;
    Wrapland::Server::drm_lease_device_v1* drm_lease_device() const;

    void create_presentation_manager();

    void remove_window(win::wayland::window* window);

    win::wayland::window* find_window(Wrapland::Server::Surface* surface) const;
    Toplevel* findToplevel(Wrapland::Server::Surface* surface) const;

    /**
     * @returns a parent of a surface imported with the foreign protocol, if any
     */
    Wrapland::Server::Surface* findForeignParentForSurface(Wrapland::Server::Surface* surface);

    /**
     * @returns file descriptor for Xwayland to connect to.
     */
    int createXWaylandConnection();
    void destroyXWaylandConnection();

    /**
     * @returns file descriptor to the input method server's socket.
     */
    int createInputMethodConnection();
    void destroyInputMethodConnection();

    void createDrmLeaseDevice();

    bool is_screen_locked() const;
    /**
     * @returns whether integration with KScreenLocker is available.
     */
    bool hasScreenLockerIntegration() const;

    /**
     * @returns whether any kind of global shortcuts are supported.
     */
    bool hasGlobalShortcutSupport() const;

    void create_addons(std::function<void()> callback);
    void initWorkspace();

    Wrapland::Server::Client* xWaylandConnection() const
    {
        return m_xwayland.client;
    }
    Wrapland::Server::Client* inputMethodConnection() const
    {
        return m_inputMethodServerConnection;
    }
    Wrapland::Server::Client* internalConnection() const
    {
        return m_internalConnection.server;
    }
    Wrapland::Server::Client* screenLockerClientConnection() const
    {
        return m_screenLockerClientConnection;
    }
    Wrapland::Client::Compositor* internalCompositor()
    {
        return m_internalConnection.compositor;
    }
    Wrapland::Client::Seat* internalSeat()
    {
        return m_internalConnection.seat;
    }
    Wrapland::Client::ShmPool* internalShmPool()
    {
        return m_internalConnection.shm;
    }
    Wrapland::Client::ConnectionThread* internalClientConection()
    {
        return m_internalConnection.client;
    }
    Wrapland::Client::Registry* internalClientRegistry()
    {
        return m_internalConnection.registry;
    }
    void dispatch();

    /**
     * Struct containing information for a created Wayland connection through a
     * socketpair.
     */
    struct SocketPairConnection {
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
    SocketPairConnection createConnection();

    void simulateUserActivity();
    void updateKeyState(input::xkb::LEDs leds);

    QSet<Wrapland::Server::LinuxDmabufBufferV1*> linuxDmabufBuffers() const
    {
        return m_linuxDmabufBuffers;
    }
    void addLinuxDmabufBuffer(Wrapland::Server::LinuxDmabufBufferV1* buffer)
    {
        m_linuxDmabufBuffers << buffer;
    }
    void removeLinuxDmabufBuffer(Wrapland::Server::LinuxDmabufBufferV1* buffer)
    {
        m_linuxDmabufBuffers.remove(buffer);
    }

Q_SIGNALS:
    void window_added(KWin::win::wayland::window*);
    void window_removed(KWin::win::wayland::window*);

    void terminatingInternalClientConnection();
    void screenlocker_initialized();
    void foreignTransientChanged(Wrapland::Server::Surface* child);

private:
    explicit WaylandServer(InitializationFlags flags);

    void create_globals();
    void createInternalConnection(std::function<void(bool)> callback);
    int createScreenLockerConnection();

    void window_shown(Toplevel* window);
    void adopt_transient_children(Toplevel* window);

    void destroyInternalConnection();
    template<class T>
    void createSurface(T* surface);
    void initScreenLocker();

    std::unique_ptr<Wrapland::Server::Display> m_display;
    std::unique_ptr<Wrapland::Server::globals> globals;

    QSet<Wrapland::Server::LinuxDmabufBufferV1*> m_linuxDmabufBuffers;

    struct {
        Wrapland::Server::Client* client = nullptr;
        QMetaObject::Connection destroyConnection;
    } m_xwayland;
    Wrapland::Server::Client* m_inputMethodServerConnection = nullptr;
    Wrapland::Server::Client* m_screenLockerClientConnection = nullptr;
    struct {
        Wrapland::Server::Client* server = nullptr;
        Wrapland::Client::ConnectionThread* client = nullptr;
        QThread* clientThread = nullptr;
        Wrapland::Client::Registry* registry = nullptr;
        Wrapland::Client::Compositor* compositor = nullptr;
        Wrapland::Client::EventQueue* queue = nullptr;
        Wrapland::Client::Seat* seat = nullptr;
        Wrapland::Client::ShmPool* shm = nullptr;

    } m_internalConnection;
    QHash<Wrapland::Server::Client*, quint16> m_clientIds;
    InitializationFlags m_initFlags;
};

}
