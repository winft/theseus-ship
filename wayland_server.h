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

#include <kwinglobals.h>
#include "keyboard_input.h"

#include <QObject>

class QThread;
class QProcess;
class QWindow;

namespace Wrapland
{
namespace Client
{
class ConnectionThread;
class Registry;
class Compositor;
class Seat;
class DataDeviceManager;
class ShmPool;
class Surface;
}
namespace Server
{
class AppMenuManagerInterface;
class ClientConnection;
class CompositorInterface;
class Display;
class DataDeviceInterface;
class IdleInterface;
class SeatInterface;
class DataDeviceManagerInterface;
class ServerSideDecorationManagerInterface;
class ServerSideDecorationPaletteManagerInterface;
class SurfaceInterface;
class OutputInterface;
class PlasmaShellInterface;
class PlasmaShellSurfaceInterface;
class PlasmaVirtualDesktopManagementInterface;
class PlasmaWindowManagementInterface;
class QtSurfaceExtensionInterface;
class OutputManagementV1Interface;
class OutputConfigurationV1Interface;
class XdgDecorationManagerInterface;
class XdgShellInterface;
class XdgForeignInterface;
class XdgOutputManagerInterface;
class KeyStateInterface;
class LinuxDmabufUnstableV1Interface;
class LinuxDmabufUnstableV1Buffer;
}
}

namespace KWin
{
class XdgShellClient;

class AbstractClient;
class Toplevel;

class KWIN_EXPORT WaylandServer : public QObject
{
    Q_OBJECT
public:
    enum class InitalizationFlag {
        NoOptions = 0x0,
        LockScreen = 0x1,
        NoLockScreenIntegration = 0x2,
        NoGlobalShortcuts = 0x4
    };

    Q_DECLARE_FLAGS(InitalizationFlags, InitalizationFlag)

    ~WaylandServer() override;
    bool init(const QByteArray &socketName = QByteArray(), InitalizationFlags flags = InitalizationFlag::NoOptions);
    void terminateClientConnections();

    Wrapland::Server::Display *display() {
        return m_display;
    }
    Wrapland::Server::CompositorInterface *compositor() {
        return m_compositor;
    }
    Wrapland::Server::SeatInterface *seat() {
        return m_seat;
    }
    Wrapland::Server::DataDeviceManagerInterface *dataDeviceManager() {
        return m_dataDeviceManager;
    }
    Wrapland::Server::PlasmaVirtualDesktopManagementInterface *virtualDesktopManagement() {
        return m_virtualDesktopManagement;
    }
    Wrapland::Server::PlasmaWindowManagementInterface *windowManagement() {
        return m_windowManagement;
    }
    Wrapland::Server::ServerSideDecorationManagerInterface *decorationManager() const {
        return m_decorationManager;
    }
    Wrapland::Server::XdgOutputManagerInterface *xdgOutputManager() const {
        return m_xdgOutputManager;
    }
    Wrapland::Server::LinuxDmabufUnstableV1Interface *linuxDmabuf();

    QList<XdgShellClient *> clients() const {
        return m_clients;
    }
    void removeClient(XdgShellClient *c);
    XdgShellClient *findClient(quint32 id) const;
    XdgShellClient *findClient(Wrapland::Server::SurfaceInterface *surface) const;
    AbstractClient *findAbstractClient(Wrapland::Server::SurfaceInterface *surface) const;

    /**
     * @returns a transient parent of a surface imported with the foreign protocol, if any
     */
    Wrapland::Server::SurfaceInterface *findForeignTransientForSurface(Wrapland::Server::SurfaceInterface *surface);

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

    void createInternalConnection();
    void initWorkspace();

    Wrapland::Server::ClientConnection *xWaylandConnection() const {
        return m_xwayland.client;
    }
    Wrapland::Server::ClientConnection *inputMethodConnection() const {
        return m_inputMethodServerConnection;
    }
    Wrapland::Server::ClientConnection *internalConnection() const {
        return m_internalConnection.server;
    }
    Wrapland::Server::ClientConnection *screenLockerClientConnection() const {
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
    quint32 createWindowId(Wrapland::Server::SurfaceInterface *surface);

    /**
     * Struct containing information for a created Wayland connection through a
     * socketpair.
     */
    struct SocketPairConnection {
        /**
         * ServerSide Connection
         */
        Wrapland::Server::ClientConnection *connection = nullptr;
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
    void updateKeyState(KWin::Xkb::LEDs leds);

    QSet<Wrapland::Server::LinuxDmabufUnstableV1Buffer*> linuxDmabufBuffers() const {
        return m_linuxDmabufBuffers;
    }
    void addLinuxDmabufBuffer(Wrapland::Server::LinuxDmabufUnstableV1Buffer *buffer) {
        m_linuxDmabufBuffers << buffer;
    }
    void removeLinuxDmabufBuffer(Wrapland::Server::LinuxDmabufUnstableV1Buffer *buffer) {
        m_linuxDmabufBuffers.remove(buffer);
    }

Q_SIGNALS:
    void shellClientAdded(KWin::XdgShellClient *);
    void shellClientRemoved(KWin::XdgShellClient *);
    void terminatingInternalClientConnection();
    void initialized();
    void foreignTransientChanged(Wrapland::Server::SurfaceInterface *child);

private:
    void shellClientShown(Toplevel *t);
    quint16 createClientId(Wrapland::Server::ClientConnection *c);
    void destroyInternalConnection();
    template <class T>
    void createSurface(T *surface);
    void initScreenLocker();
    Wrapland::Server::Display *m_display = nullptr;
    Wrapland::Server::CompositorInterface *m_compositor = nullptr;
    Wrapland::Server::SeatInterface *m_seat = nullptr;
    Wrapland::Server::DataDeviceManagerInterface *m_dataDeviceManager = nullptr;
    Wrapland::Server::XdgShellInterface *m_xdgShell6 = nullptr;
    Wrapland::Server::XdgShellInterface *m_xdgShell = nullptr;
    Wrapland::Server::PlasmaShellInterface *m_plasmaShell = nullptr;
    Wrapland::Server::PlasmaWindowManagementInterface *m_windowManagement = nullptr;
    Wrapland::Server::PlasmaVirtualDesktopManagementInterface *m_virtualDesktopManagement = nullptr;
    Wrapland::Server::ServerSideDecorationManagerInterface *m_decorationManager = nullptr;
    Wrapland::Server::OutputManagementV1Interface *m_outputManagement = nullptr;
    Wrapland::Server::AppMenuManagerInterface *m_appMenuManager = nullptr;
    Wrapland::Server::ServerSideDecorationPaletteManagerInterface *m_paletteManager = nullptr;
    Wrapland::Server::IdleInterface *m_idle = nullptr;
    Wrapland::Server::XdgOutputManagerInterface *m_xdgOutputManager = nullptr;
    Wrapland::Server::XdgDecorationManagerInterface *m_xdgDecorationManager = nullptr;
    Wrapland::Server::LinuxDmabufUnstableV1Interface *m_linuxDmabuf = nullptr;
    QSet<Wrapland::Server::LinuxDmabufUnstableV1Buffer*> m_linuxDmabufBuffers;
    struct {
        Wrapland::Server::ClientConnection *client = nullptr;
        QMetaObject::Connection destroyConnection;
    } m_xwayland;
    Wrapland::Server::ClientConnection *m_inputMethodServerConnection = nullptr;
    Wrapland::Server::ClientConnection *m_screenLockerClientConnection = nullptr;
    struct {
        Wrapland::Server::ClientConnection *server = nullptr;
        Wrapland::Client::ConnectionThread *client = nullptr;
        QThread *clientThread = nullptr;
        Wrapland::Client::Registry *registry = nullptr;
        Wrapland::Client::Compositor *compositor = nullptr;
        Wrapland::Client::Seat *seat = nullptr;
        Wrapland::Client::DataDeviceManager *ddm = nullptr;
        Wrapland::Client::ShmPool *shm = nullptr;
        bool interfacesAnnounced = false;

    } m_internalConnection;
    Wrapland::Server::XdgForeignInterface *m_XdgForeign = nullptr;
    Wrapland::Server::KeyStateInterface *m_keyState = nullptr;
    QList<XdgShellClient *> m_clients;
    QHash<Wrapland::Server::ClientConnection*, quint16> m_clientIds;
    InitalizationFlags m_initFlags;
    QVector<Wrapland::Server::PlasmaShellSurfaceInterface*> m_plasmaShellSurfaces;
    KWIN_SINGLETON(WaylandServer)
};

inline
WaylandServer *waylandServer() {
    return WaylandServer::self();
}

} // namespace KWin

#endif

