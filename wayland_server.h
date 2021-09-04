/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>

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
#ifndef KWIN_WAYLAND_SERVER_H
#define KWIN_WAYLAND_SERVER_H

#include "input/xkb.h"

#include <kwinglobals.h>

#include <QObject>

class QThread;
class QProcess;
class QWindow;

namespace Wrapland
{
namespace Client
{
class ConnectionThread;
class EventQueue;
class Registry;
class Compositor;
class Seat;
class DataDeviceManager;
class PrimarySelectionDeviceManager;
class ShmPool;
class Surface;
}
namespace Server
{
class AppmenuManager;
class Client;
class Compositor;
class Display;
class DataDevice;
class drm_lease_device_v1;
class KdeIdle;
class Seat;
class data_device_manager;
class ServerSideDecorationPaletteManager;
class Surface;
class Output;
class PlasmaShell;
class PlasmaShellSurface;
class PlasmaVirtualDesktopManager;
class PlasmaWindowManager;
class PresentationManager;
class primary_selection_device_manager;
class QtSurfaceExtension;
class OutputManagementV1;
class OutputConfigurationV1;
class Subcompositor;
class XdgDecorationManager;
class XdgShell;
class XdgForeign;
class KeyState;
class LayerShellV1;
class LinuxDmabufV1;
class LinuxDmabufBufferV1;
class Viewporter;
class XdgActivationV1;
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

    Wrapland::Server::LayerShellV1* layer_shell{nullptr};
    Wrapland::Server::XdgActivationV1* xdg_activation{nullptr};
    Wrapland::Server::drm_lease_device_v1* drm_lease_device{nullptr};

    WaylandServer(std::string const& socket, InitializationFlags flags);
    WaylandServer(int socket_fd, InitializationFlags flags);
    ~WaylandServer() override;

    void terminateClientConnections();

    Wrapland::Server::Display *display() {
        return m_display;
    }
    Wrapland::Server::Compositor *compositor() {
        return m_compositor;
    }
    Wrapland::Server::Subcompositor* subcompositor{nullptr};
    Wrapland::Server::Seat *seat() {
        return m_seat;
    }
    Wrapland::Server::data_device_manager *dataDeviceManager() {
        return m_dataDeviceManager;
    }
    Wrapland::Server::primary_selection_device_manager *primarySelectionDeviceManager() const {
        return m_primarySelectionDeviceManager;
    }
    Wrapland::Server::PlasmaVirtualDesktopManager *virtualDesktopManagement() {
        return m_virtualDesktopManagement;
    }
    Wrapland::Server::PlasmaWindowManager *windowManagement() {
        return m_windowManagement;
    }
    Wrapland::Server::XdgShell *xdgShell() const {
        return m_xdgShell;
    }
    Wrapland::Server::Viewporter *viewporter() const {
        return m_viewporter;
    }
    Wrapland::Server::LinuxDmabufV1 *linuxDmabuf();

    Wrapland::Server::PresentationManager *presentationManager() const;
    void createPresentationManager();

    void remove_window(win::wayland::window* window);

    win::wayland::window* find_window(Wrapland::Server::Surface* surface) const;
    Toplevel* findToplevel(Wrapland::Server::Surface *surface) const;

    /**
     * @returns a parent of a surface imported with the foreign protocol, if any
     */
    Wrapland::Server::Surface *findForeignParentForSurface(Wrapland::Server::Surface *surface);

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

    /**
     * @returns true if screen is locked.
     */
    bool isScreenLocked() const;
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

    Wrapland::Server::Client *xWaylandConnection() const {
        return m_xwayland.client;
    }
    Wrapland::Server::Client *inputMethodConnection() const {
        return m_inputMethodServerConnection;
    }
    Wrapland::Server::Client *internalConnection() const {
        return m_internalConnection.server;
    }
    Wrapland::Server::Client *screenLockerClientConnection() const {
        return m_screenLockerClientConnection;
    }
    Wrapland::Client::Compositor *internalCompositor() {
        return m_internalConnection.compositor;
    }
    Wrapland::Client::Seat *internalSeat() {
        return m_internalConnection.seat;
    }
    Wrapland::Client::DataDeviceManager *internalDataDeviceManager() {
        return m_internalConnection.ddm;
    }
    Wrapland::Client::PrimarySelectionDeviceManager *internalPrimarySelectionDeviceManager() {
        return m_internalConnection.psdm;
    }
    Wrapland::Client::ShmPool *internalShmPool() {
        return m_internalConnection.shm;
    }
    Wrapland::Client::ConnectionThread *internalClientConection() {
        return m_internalConnection.client;
    }
    Wrapland::Client::Registry *internalClientRegistry() {
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
        Wrapland::Server::Client *connection = nullptr;
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

    QSet<Wrapland::Server::LinuxDmabufBufferV1*> linuxDmabufBuffers() const {
        return m_linuxDmabufBuffers;
    }
    void addLinuxDmabufBuffer(Wrapland::Server::LinuxDmabufBufferV1 *buffer) {
        m_linuxDmabufBuffers << buffer;
    }
    void removeLinuxDmabufBuffer(Wrapland::Server::LinuxDmabufBufferV1 *buffer) {
        m_linuxDmabufBuffers.remove(buffer);
    }

Q_SIGNALS:
    void window_added(KWin::win::wayland::window*);
    void window_removed(KWin::win::wayland::window*);

    void terminatingInternalClientConnection();
    void screenlocker_initialized();
    void foreignTransientChanged(Wrapland::Server::Surface *child);

private:
    explicit WaylandServer(InitializationFlags flags);

    void create_globals();
    void createInternalConnection(std::function<void(bool)> callback);
    int createScreenLockerConnection();

    void window_shown(Toplevel* window);
    void adopt_transient_children(Toplevel* window);

    void destroyInternalConnection();
    template <class T>
    void createSurface(T *surface);
    void initScreenLocker();
    Wrapland::Server::Display *m_display = nullptr;
    Wrapland::Server::Compositor *m_compositor = nullptr;
    Wrapland::Server::Seat *m_seat = nullptr;
    Wrapland::Server::data_device_manager *m_dataDeviceManager = nullptr;
    Wrapland::Server::XdgShell *m_xdgShell = nullptr;
    Wrapland::Server::PlasmaShell *m_plasmaShell = nullptr;
    Wrapland::Server::PlasmaWindowManager *m_windowManagement = nullptr;
    Wrapland::Server::PlasmaVirtualDesktopManager *m_virtualDesktopManagement = nullptr;
    Wrapland::Server::PresentationManager *m_presentationManager = nullptr;
    Wrapland::Server::primary_selection_device_manager *m_primarySelectionDeviceManager = nullptr;
    Wrapland::Server::OutputManagementV1 *m_outputManagement = nullptr;
    Wrapland::Server::AppmenuManager *m_appmenuManager = nullptr;
    Wrapland::Server::ServerSideDecorationPaletteManager *m_paletteManager = nullptr;
    Wrapland::Server::KdeIdle *m_idle = nullptr;
    Wrapland::Server::Viewporter *m_viewporter = nullptr;
    Wrapland::Server::XdgDecorationManager *m_xdgDecorationManager = nullptr;
    Wrapland::Server::LinuxDmabufV1 *m_linuxDmabuf = nullptr;
    QSet<Wrapland::Server::LinuxDmabufBufferV1*> m_linuxDmabufBuffers;
    struct {
        Wrapland::Server::Client *client = nullptr;
        QMetaObject::Connection destroyConnection;
    } m_xwayland;
    Wrapland::Server::Client *m_inputMethodServerConnection = nullptr;
    Wrapland::Server::Client *m_screenLockerClientConnection = nullptr;
    struct {
        Wrapland::Server::Client *server = nullptr;
        Wrapland::Client::ConnectionThread *client = nullptr;
        QThread *clientThread = nullptr;
        Wrapland::Client::Registry *registry = nullptr;
        Wrapland::Client::Compositor *compositor = nullptr;
        Wrapland::Client::EventQueue *queue = nullptr;
        Wrapland::Client::Seat *seat = nullptr;
        Wrapland::Client::DataDeviceManager *ddm = nullptr;
        Wrapland::Client::PrimarySelectionDeviceManager *psdm = nullptr;
        Wrapland::Client::ShmPool *shm = nullptr;

    } m_internalConnection;
    Wrapland::Server::XdgForeign *m_XdgForeign = nullptr;
    Wrapland::Server::KeyState *m_keyState = nullptr;
    QHash<Wrapland::Server::Client*, quint16> m_clientIds;
    InitializationFlags m_initFlags;

    static WaylandServer* s_self;
};

inline
WaylandServer *waylandServer() {
    return WaylandServer::self();
}

} // namespace KWin

#endif

