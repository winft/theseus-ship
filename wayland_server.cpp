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
#include "wayland_server.h"
#include "platform.h"
#include "idle_inhibition.h"
#include "screens.h"
#include "workspace.h"
#include "service_utils.h"

#include "win/wayland/layer_shell.h"
#include "win/wayland/subsurface.h"
#include "win/wayland/window.h"
#include "win/wayland/xdg_activation.h"
#include "win/wayland/xdg_shell.h"

// Client
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/event_queue.h>
#include <Wrapland/Client/registry.h>
#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/datadevicemanager.h>
#include <Wrapland/Client/primary_selection.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/surface.h>
// Server
#include <Wrapland/Server/appmenu.h>
#include <Wrapland/Server/client.h>
#include <Wrapland/Server/compositor.h>
#include <Wrapland/Server/data_device_manager.h>
#include <Wrapland/Server/data_source.h>
#include <Wrapland/Server/display.h>
#include <Wrapland/Server/dpms.h>
#include <Wrapland/Server/kde_idle.h>
#include <Wrapland/Server/idle_inhibit_v1.h>
#include <Wrapland/Server/layer_shell_v1.h>
#include <Wrapland/Server/linux_dmabuf_v1.h>
#include <Wrapland/Server/output.h>
#include <Wrapland/Server/plasma_shell.h>
#include <Wrapland/Server/plasma_virtual_desktop.h>
#include <Wrapland/Server/plasma_window.h>
#include <Wrapland/Server/pointer_constraints_v1.h>
#include <Wrapland/Server/pointer_gestures_v1.h>
#include <Wrapland/Server/presentation_time.h>
#include <Wrapland/Server/primary_selection.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/server_decoration_palette.h>
#include <Wrapland/Server/shadow.h>
#include <Wrapland/Server/subcompositor.h>
#include <Wrapland/Server/surface.h>
#include <Wrapland/Server/blur.h>
#include <Wrapland/Server/output_management_v1.h>
#include <Wrapland/Server/output_configuration_v1.h>
#include <Wrapland/Server/viewporter.h>
#include <Wrapland/Server/xdg_decoration.h>
#include <Wrapland/Server/xdg_shell.h>
#include <Wrapland/Server/xdg_foreign.h>
#include <Wrapland/Server/keystate.h>
#include <Wrapland/Server/filtered_display.h>

#include <KWayland/Server/display.h>

// Qt
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QWindow>

// system
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

//screenlocker
#include <KScreenLocker/KsldApp>

using namespace Wrapland::Server;

namespace KWin
{

class KWinDisplay : public Wrapland::Server::FilteredDisplay
{
public:
    KWinDisplay(QObject *parent)
        : Wrapland::Server::FilteredDisplay(parent)
    {}

    static QByteArray sha256(const QString &fileName)
    {
        QFile f(fileName);
        if (f.open(QFile::ReadOnly)) {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (hash.addData(&f)) {
                return hash.result();
            }
        }
        return QByteArray();
    }

    bool isTrustedOrigin(Wrapland::Server::Client *client) const {
        const auto fullPathSha = sha256(QString::fromStdString(client->executablePath()));
        const auto localSha = sha256(QLatin1String("/proc/") + QString::number(client->processId()) + QLatin1String("/exe"));
        const bool trusted = !localSha.isEmpty() && fullPathSha == localSha;

        if (!trusted) {
            qCWarning(KWIN_CORE) << "Could not trust" << client->executablePath().c_str() << "sha" << localSha << fullPathSha;
        }

        return trusted;
    }

    QStringList fetchRequestedInterfaces(Wrapland::Server::Client *client) const {
        return KWin::fetchRequestedInterfaces(client->executablePath().c_str());
    }

    const QSet<QByteArray> interfacesBlackList = {"org_kde_kwin_remote_access_manager", "org_kde_plasma_window_management", "org_kde_kwin_fake_input", "org_kde_kwin_keystate"};
    QSet<QString> m_reported;

    bool allowInterface(Wrapland::Server::Client *client, const QByteArray &interfaceName) override {
        if (client->processId() == getpid()) {
            return true;
        }

        if (!interfacesBlackList.contains(interfaceName)) {
            return true;
        }

        if (client->executablePath().empty()) {
            qCDebug(KWIN_CORE) << "Could not identify process with pid" << client->processId();
            return false;
        }

        {
            auto requestedInterfaces = client->property("requestedInterfaces");
            if (requestedInterfaces.isNull()) {
                requestedInterfaces = fetchRequestedInterfaces(client);
                client->setProperty("requestedInterfaces", requestedInterfaces);
            }
            if (!requestedInterfaces.toStringList().contains(QString::fromUtf8(interfaceName))) {
                if (KWIN_CORE().isDebugEnabled()) {
                    const QString id = QString::fromStdString(client->executablePath()) + QLatin1Char('|') + QString::fromUtf8(interfaceName);
                    if (!m_reported.contains({id})) {
                        m_reported.insert(id);
                        qCDebug(KWIN_CORE) << "Interface" << interfaceName << "not in X-KDE-Wayland-Interfaces of" << client->executablePath().c_str();
                    }
                }
                return false;
            }
        }

        {
            auto trustedOrigin = client->property("isPrivileged");
            if (trustedOrigin.isNull()) {
                trustedOrigin = isTrustedOrigin(client);
                client->setProperty("isPrivileged", trustedOrigin);
            }

            if (!trustedOrigin.toBool()) {
                return false;
            }
        }
        qCDebug(KWIN_CORE) << "authorized" << client->executablePath().c_str() << interfaceName;
        return true;
    }
};

WaylandServer* WaylandServer::s_self{nullptr};
WaylandServer* WaylandServer::self()
{
    return s_self;
}

WaylandServer::WaylandServer(InitializationFlags flags)
    : QObject()
    , m_display(new KWinDisplay(this))
    , m_initFlags{flags}

{
    qRegisterMetaType<Wrapland::Server::Output::DpmsMode>();

    assert(!s_self);
    s_self = this;
}

WaylandServer::WaylandServer(std::string const& socket, InitializationFlags flags)
    : WaylandServer(flags)

{
    m_display->setSocketName(socket);
    m_display->start(Wrapland::Server::Display::StartMode::ConnectToSocket);
    create_globals();
}

WaylandServer::WaylandServer(int socket_fd, InitializationFlags flags)
    : WaylandServer(flags)

{
    m_display->add_socket_fd(socket_fd);
    m_display->start(Wrapland::Server::Display::StartMode::ConnectClientsOnly);
    create_globals();
}

WaylandServer::~WaylandServer()
{
    destroyInputMethodConnection();
}

void WaylandServer::destroyInternalConnection()
{
    emit terminatingInternalClientConnection();
    if (m_internalConnection.client) {
        // delete all connections hold by plugins like e.g. widget style
        const auto connections = Wrapland::Client::ConnectionThread::connections();
        for (auto c : connections) {
            if (c == m_internalConnection.client) {
                continue;
            }
            Q_EMIT c->establishedChanged(false);
        }

        delete m_internalConnection.registry;
        delete m_internalConnection.compositor;
        delete m_internalConnection.seat;
        delete m_internalConnection.ddm;
        delete m_internalConnection.psdm;
        delete m_internalConnection.shm;
        dispatch();
        delete m_internalConnection.queue;
        m_internalConnection.client->deleteLater();
        m_internalConnection.clientThread->quit();
        m_internalConnection.clientThread->wait();
        delete m_internalConnection.clientThread;
        m_internalConnection.client = nullptr;
        m_internalConnection.server->destroy();
        m_internalConnection.server = nullptr;
    }
}

void WaylandServer::terminateClientConnections()
{
    destroyInternalConnection();
    destroyInputMethodConnection();

    for (auto client : m_display->clients()) {
        client->destroy();
    }
}

void WaylandServer::create_globals()
{
    if (!m_display->running()) {
        s_self = nullptr;
        throw std::exception();
    }

    m_compositor = m_display->createCompositor(m_display);

    connect(m_compositor, &Wrapland::Server::Compositor::surfaceCreated, this,
        [this] (Surface *surface) {
            // check whether we have a Toplevel with the Surface's id
            Workspace *ws = Workspace::self();
            if (!ws) {
                // it's possible that a Surface gets created before Workspace is created
                return;
            }
            if (surface->client() != xWaylandConnection()) {
                // setting surface is only relevat for Xwayland clients
                return;
            }
            auto check = [surface] (const Toplevel *t) {
                return t->surfaceId() == surface->id();
            };
            if (Toplevel *t = ws->findToplevel(check)) {
                t->setSurface(surface);
            }
        }
    );

    m_xdgShell = m_display->createXdgShell(m_display);
    connect(m_xdgShell, &XdgShell::toplevelCreated, this, [this](XdgShellToplevel* toplevel) {
        if (!Workspace::self()) {
            // it's possible that a Surface gets created before Workspace is created
            return;
        }
        if (toplevel->client() == m_screenLockerClientConnection) {
            ScreenLocker::KSldApp::self()->lockScreenShown();
        }
        auto window = win::wayland::create_toplevel_window(toplevel);

        // TODO: Also relevant for popups?
        auto it = std::find_if(
            m_plasmaShellSurfaces.begin(),
            m_plasmaShellSurfaces.end(),
            [window](auto shell_surface) { return window->surface() == shell_surface->surface(); });
        if (it != m_plasmaShellSurfaces.end()) {
            win::wayland::install_plasma_shell_surface(window, *it);
            m_plasmaShellSurfaces.erase(it);
        }

        if (auto menu = m_appmenuManager->appmenuForSurface(window->surface())) {
            win::wayland::install_appmenu(window, menu);
        }
        if (auto palette = m_paletteManager->paletteForSurface(toplevel->surface()->surface())) {
            win::wayland::install_palette(window, palette);
        }

        windows.push_back(window);

        if (window->readyForPainting()) {
            Q_EMIT window_added(window);
        } else {
            connect(window, &win::wayland::window::windowShown, this, &WaylandServer::window_shown);
        }

        //not directly connected as the connection is tied to client instead of this
        connect(m_XdgForeign, &Wrapland::Server::XdgForeign::parentChanged,
                window, [this]([[maybe_unused]] Wrapland::Server::Surface* parent,
                               Wrapland::Server::Surface* child) {
            Q_EMIT foreignTransientChanged(child);
        });
    });
    connect(m_xdgShell, &XdgShell::popupCreated, this, [this](XdgShellPopup* popup) {
        if (!Workspace::self()) {
            // it's possible that a Surface gets created before Workspace is created
            return;
        }
        auto window = win::wayland::create_popup_window(popup);
        windows.push_back(window);

        if (window->readyForPainting()) {
            Q_EMIT window_added(window);
        } else {
            connect(window, &win::wayland::window::windowShown, this, &WaylandServer::window_shown);
        }
    });

    m_xdgDecorationManager = m_display->createXdgDecorationManager(m_xdgShell, m_display);
    connect(m_xdgDecorationManager, &XdgDecorationManager::decorationCreated, this,  [this] (XdgDecoration *deco) {
        if (auto win = find_window(deco->toplevel()->surface()->surface())) {
            win::wayland::install_deco(win, deco);
        }
    });

    m_display->createShm();
    m_seat = m_display->createSeat(m_display);

    m_display->createPointerGestures(m_display);
    m_display->createPointerConstraints(m_display);
    m_dataDeviceManager = m_display->createDataDeviceManager(m_display);
    m_primarySelectionDeviceManager = m_display->createPrimarySelectionDeviceManager(m_display);
    m_idle = m_display->createIdle(m_display);

    auto idleInhibition = new IdleInhibition(m_idle);
    connect(this, &WaylandServer::window_added, idleInhibition, &IdleInhibition::register_window);
    m_display->createIdleInhibitManager(m_display);

    m_plasmaShell = m_display->createPlasmaShell(m_display);
    connect(m_plasmaShell, &PlasmaShell::surfaceCreated,
        [this] (PlasmaShellSurface *surface) {
            if (auto win = find_window(surface->surface())) {
                assert (win->toplevel || win->popup || win->layer_surface);
                win::wayland::install_plasma_shell_surface(win, surface);
            } else {
                m_plasmaShellSurfaces << surface;
                connect(surface, &QObject::destroyed, this,
                    [this, surface] {
                        m_plasmaShellSurfaces.removeOne(surface);
                    }
                );
            }
        }
    );
    m_appmenuManager = m_display->createAppmenuManager(m_display);
    connect(m_appmenuManager, &AppmenuManager::appmenuCreated,
        [this] (Appmenu *appMenu) {
            if (auto win = find_window(appMenu->surface())) {
                if (win->control) {
                    // Need to check that as plasma-integration creates them blindly even for
                    // xdg-shell popups.
                    win::wayland::install_appmenu(win, appMenu);
                }
            }
        }
    );

    m_paletteManager = m_display->createServerSideDecorationPaletteManager(m_display);
    connect(m_paletteManager, &ServerSideDecorationPaletteManager::paletteCreated,
        [this] (ServerSideDecorationPalette *palette) {
            if (auto win = find_window(palette->surface())) {
                if (win->control) {
                    win::wayland::install_palette(win, palette);
                }
            }
        }
    );

    m_windowManagement = m_display->createPlasmaWindowManager(m_display);
    m_windowManagement->setShowingDesktopState(PlasmaWindowManager::ShowingDesktopState::Disabled);
    connect(m_windowManagement, &PlasmaWindowManager::requestChangeShowingDesktop, this,
        [] (PlasmaWindowManager::ShowingDesktopState state) {
            if (!workspace()) {
                return;
            }
            bool set = false;
            switch (state) {
            case PlasmaWindowManager::ShowingDesktopState::Disabled:
                set = false;
                break;
            case PlasmaWindowManager::ShowingDesktopState::Enabled:
                set = true;
                break;
            default:
                Q_UNREACHABLE();
                break;
            }
            if (set == workspace()->showingDesktop()) {
                return;
            }
            workspace()->setShowingDesktop(set);
        }
    );


    m_virtualDesktopManagement = m_display->createPlasmaVirtualDesktopManager(m_display);
    m_windowManagement->setVirtualDesktopManager(m_virtualDesktopManagement);

    m_display->createShadowManager(m_display);
    m_display->createDpmsManager(m_display);

    m_outputManagement = m_display->createOutputManagementV1(m_display);
    connect(m_outputManagement, &OutputManagementV1::configurationChangeRequested,
            this, [](Wrapland::Server::OutputConfigurationV1 *config) {
                kwinApp()->platform()->requestOutputsChange(config);
    });

    subcompositor = m_display->createSubCompositor(m_display);
    connect(subcompositor,
            &Wrapland::Server::Subcompositor::subsurfaceCreated,
            this,
            [this](auto subsurface) {
                auto window = new win::wayland::window(subsurface->surface());

                windows.push_back(window);
                QObject::connect(subsurface,
                                 &Wrapland::Server::Subsurface::resourceDestroyed,
                                 this,
                                 [this, window] { remove_all(windows, window); });

                win::wayland::assign_subsurface_role(window);

                for (auto& win : windows) {
                    if (win->surface() == subsurface->parentSurface()) {
                        win::wayland::set_subsurface_parent(window, win);
                        if (window->readyForPainting()) {
                            Q_EMIT window_added(window);
                            adopt_transient_children(window);
                            return;
                        }
                        break;
                    }
                }
                // Must wait till a parent is mapped and subsurface is ready for painting.
                connect(window, &win::wayland::window::windowShown, this, &WaylandServer::window_shown);
            });

    layer_shell = m_display->createLayerShellV1(m_display);
    connect(layer_shell,
            &Wrapland::Server::LayerShellV1::surface_created,
            this,
            [this](auto layer_surface) {
                auto window = new win::wayland::window(layer_surface->surface());
                if (layer_surface->surface()->client() == m_screenLockerClientConnection) {
                    ScreenLocker::KSldApp::self()->lockScreenShown();
                }

                windows.push_back(window);
                QObject::connect(layer_surface,
                                 &Wrapland::Server::LayerSurfaceV1::resourceDestroyed,
                                 this,
                                 [this, window] { remove_all(windows, window); });

                win::wayland::assign_layer_surface_role(window, layer_surface);

                if (window->readyForPainting()) {
                    Q_EMIT window_added(window);
                } else {
                    connect(window, &win::wayland::window::windowShown, this, &WaylandServer::window_shown);
                }
            });

    xdg_activation = m_display->createXdgActivationV1(m_display);
    m_XdgForeign = m_display->createXdgForeign(m_display);

    m_keyState = m_display->createKeyState(m_display);
    m_viewporter = m_display->createViewporter(m_display);
}

Wrapland::Server::PresentationManager* WaylandServer::presentationManager() const
{
    return m_presentationManager;
}

void WaylandServer::createPresentationManager()
{
    Q_ASSERT(!m_presentationManager);
    m_presentationManager = m_display->createPresentationManager(m_display);
}

  Wrapland::Server::LinuxDmabufV1 *WaylandServer::linuxDmabuf()
{
    if (!m_linuxDmabuf) {
        m_linuxDmabuf = m_display->createLinuxDmabuf(m_display);
    }
    return m_linuxDmabuf;
}

Surface *WaylandServer::findForeignParentForSurface(Surface *surface)
{
    return m_XdgForeign->parentOf(surface);
}

void WaylandServer::window_shown(Toplevel* window)
{
    disconnect(window, &Toplevel::windowShown, this, &WaylandServer::window_shown);
    Q_EMIT window_added(static_cast<win::wayland::window*>(window));
    adopt_transient_children(window);
}

void WaylandServer::adopt_transient_children(Toplevel* window)
{
    std::for_each(
        windows.cbegin(), windows.cend(), [&window](auto win) { win->checkTransient(window); });
}

void WaylandServer::initWorkspace()
{
    auto ws = workspace();

    VirtualDesktopManager::self()->setVirtualDesktopManagement(m_virtualDesktopManagement);

    if (m_windowManagement) {
        connect(ws, &Workspace::showingDesktopChanged, this,
            [this] (bool set) {
                using namespace Wrapland::Server;
                m_windowManagement->setShowingDesktopState(set ?
                    PlasmaWindowManager::ShowingDesktopState::Enabled :
                    PlasmaWindowManager::ShowingDesktopState::Disabled
                );
            }
        );
    }

    connect(xdg_activation,
            &Wrapland::Server::XdgActivationV1::token_requested,
            ws,
            [ws](auto token) { win::wayland::xdg_activation_create_token(ws, token); });
    connect(xdg_activation,
            &Wrapland::Server::XdgActivationV1::activate,
            ws,
            [ws, this](auto const& token, auto surface) {
                auto win = find_window(surface);
                if (!win) {
                    qCDebug(KWIN_CORE) << "No window found to xdg-activate" << surface;
                    return;
                }
                win::wayland::xdg_activation_activate(ws, win, token);
            });

    if (hasScreenLockerIntegration()) {
        if (m_internalConnection.interfacesAnnounced) {
            initScreenLocker();
        } else {
            connect(m_internalConnection.registry, &Wrapland::Client::Registry::interfacesAnnounced, this, &WaylandServer::initScreenLocker);
        }
    } else {
        emit initialized();
    }
}

void WaylandServer::initScreenLocker()
{
    auto *screenLockerApp = ScreenLocker::KSldApp::self();

    ScreenLocker::KSldApp::self()->setGreeterEnvironment(kwinApp()->processStartupEnvironment());
    ScreenLocker::KSldApp::self()->initialize();

    connect(ScreenLocker::KSldApp::self(), &ScreenLocker::KSldApp::aboutToLock, this,
        [this, screenLockerApp] () {
            if (m_screenLockerClientConnection) {
                // Already sent data to KScreenLocker.
                return;
            }
            int clientFd = createScreenLockerConnection();
            if (clientFd < 0) {
                return;
            }
            ScreenLocker::KSldApp::self()->setWaylandFd(clientFd);

            for (auto *seat : m_display->seats()) {
                connect(seat, &Wrapland::Server::Seat::timestampChanged,
                        screenLockerApp, &ScreenLocker::KSldApp::userActivity);
            }
        }
    );

    connect(ScreenLocker::KSldApp::self(), &ScreenLocker::KSldApp::unlocked, this,
        [this, screenLockerApp] () {
            if (m_screenLockerClientConnection) {
                m_screenLockerClientConnection->destroy();
                delete m_screenLockerClientConnection;
                m_screenLockerClientConnection = nullptr;
            }

            for (auto *seat : m_display->seats()) {
                disconnect(seat, &Wrapland::Server::Seat::timestampChanged,
                           screenLockerApp, &ScreenLocker::KSldApp::userActivity);
            }
            ScreenLocker::KSldApp::self()->setWaylandFd(-1);
        }
    );

    if (m_initFlags.testFlag(InitializationFlag::LockScreen)) {
        ScreenLocker::KSldApp::self()->lock(ScreenLocker::EstablishLock::Immediate);
    }
    emit initialized();
}

WaylandServer::SocketPairConnection WaylandServer::createConnection()
{
    SocketPairConnection ret;
    int sx[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sx) < 0) {
        qCWarning(KWIN_CORE) << "Could not create socket";
        return ret;
    }
    ret.connection = m_display->createClient(sx[0]);
    ret.fd = sx[1];
    return ret;
}

int WaylandServer::createScreenLockerConnection()
{
    const auto socket = createConnection();
    if (!socket.connection) {
        return -1;
    }
    m_screenLockerClientConnection = socket.connection;
    connect(m_screenLockerClientConnection, &Wrapland::Server::Client::disconnected,
            this, [this] { m_screenLockerClientConnection = nullptr; });
    return socket.fd;
}

int WaylandServer::createXWaylandConnection()
{
    const auto socket = createConnection();
    if (!socket.connection) {
        return -1;
    }
    m_xwayland.client = socket.connection;
    m_xwayland.destroyConnection = connect(m_xwayland.client, &Wrapland::Server::Client::disconnected, this,
        [] {
            qFatal("Xwayland Connection died");
        }
    );
    return socket.fd;
}

void WaylandServer::destroyXWaylandConnection()
{
    if (!m_xwayland.client) {
        return;
    }
    disconnect(m_xwayland.destroyConnection);
    m_xwayland.client->destroy();
    m_xwayland.client = nullptr;
}

int WaylandServer::createInputMethodConnection()
{
    const auto socket = createConnection();
    if (!socket.connection) {
        return -1;
    }
    m_inputMethodServerConnection = socket.connection;
    return socket.fd;
}

void WaylandServer::destroyInputMethodConnection()
{
    if (!m_inputMethodServerConnection) {
        return;
    }
    m_inputMethodServerConnection->destroy();
    m_inputMethodServerConnection = nullptr;
}

void WaylandServer::createInternalConnection()
{
    const auto socket = createConnection();
    if (!socket.connection) {
        return;
    }
    m_internalConnection.server = socket.connection;
    using namespace Wrapland::Client;
    m_internalConnection.client = new ConnectionThread();
    m_internalConnection.client->setSocketFd(socket.fd);
    m_internalConnection.clientThread = new QThread;
    m_internalConnection.client->moveToThread(m_internalConnection.clientThread);
    m_internalConnection.clientThread->start();

    connect(m_internalConnection.client, &ConnectionThread::establishedChanged, this,
        [this](bool established) {
            if (!established) {
                return;
            }
            Registry *registry = new Registry(this);
            EventQueue *eventQueue = new EventQueue(this);
            eventQueue->setup(m_internalConnection.client);
            registry->setEventQueue(eventQueue);
            registry->create(m_internalConnection.client);
            m_internalConnection.registry = registry;
            m_internalConnection.queue = eventQueue;
            connect(registry, &Registry::shmAnnounced, this,
                [this] (quint32 name, quint32 version) {
                    m_internalConnection.shm = m_internalConnection.registry->createShmPool(name, version, this);
                }
            );
            connect(registry, &Registry::interfacesAnnounced, this,
                [this, registry] {
                    m_internalConnection.interfacesAnnounced = true;

                    const auto compInterface = registry->interface(Registry::Interface::Compositor);
                    if (compInterface.name != 0) {
                        m_internalConnection.compositor = registry->createCompositor(compInterface.name, compInterface.version, this);
                    }
                    const auto seatInterface = registry->interface(Registry::Interface::Seat);
                    if (seatInterface.name != 0) {
                        m_internalConnection.seat = registry->createSeat(seatInterface.name, seatInterface.version, this);
                    }
                    const auto ddmInterface = registry->interface(Registry::Interface::DataDeviceManager);
                    if (ddmInterface.name != 0) {
                        m_internalConnection.ddm = registry->createDataDeviceManager(ddmInterface.name, ddmInterface.version, this);
                    }
                    const auto psdmInterface = registry->interface(Registry::Interface::PrimarySelectionDeviceManager);
                    if (psdmInterface.name != 0) {
                        m_internalConnection.psdm = registry->createPrimarySelectionDeviceManager(psdmInterface.name, psdmInterface.version, this);
                    }
                }
            );
            registry->setup();
        }
    );
    m_internalConnection.client->establishConnection();
}

void WaylandServer::remove_window(win::wayland::window* window)
{
    remove_all(windows, window);
    Q_EMIT window_removed(window);
}

void WaylandServer::dispatch()
{
    if (!m_display) {
        return;
    }
    if (m_internalConnection.server) {
        m_internalConnection.server->flush();
    }
    m_display->dispatchEvents(0);
}

win::wayland::window* WaylandServer::find_window(quint32 id) const
{
    auto it = std::find_if(windows.cbegin(), windows.cend(), [id](auto win) {
        return win->windowId() == id;
    });
    return it != windows.cend() ? *it : nullptr;
}

win::wayland::window* WaylandServer::find_window(Wrapland::Server::Surface* surface) const
{
    if (!surface) {
        return nullptr;
    }
    auto it = std::find_if(windows.cbegin(), windows.cend(), [surface](auto win) {
        return win->surface() == surface;
    });
    return it != windows.cend() ? *it : nullptr;
}

Toplevel* WaylandServer::findToplevel(Surface *surface) const
{
    return find_window(surface);
}

quint32 WaylandServer::createWindowId(Surface *surface)
{
    auto it = m_clientIds.constFind(surface->client());
    quint16 clientId = 0;
    if (it != m_clientIds.constEnd()) {
        clientId = it.value();
    } else {
        clientId = createClientId(surface->client());
    }
    Q_ASSERT(clientId != 0);
    quint32 id = clientId;
    // TODO: this does not prevent that two surfaces of same client get same id
    id = (id << 16) | (surface->id() & 0xFFFF);
    if (find_window(id)) {
        qCWarning(KWIN_CORE) << "Invalid client windowId generated:" << id;
        return 0;
    }
    return id;
}

quint16 WaylandServer::createClientId(Client *c)
{
    auto ids = m_clientIds.values().toSet();
    quint16 id = 1;
    if (!ids.isEmpty()) {
        for (quint16 i = ids.count() + 1; i >= 1 ; i--) {
            if (!ids.contains(i)) {
                id = i;
                break;
            }
        }
    }
    Q_ASSERT(!ids.contains(id));
    m_clientIds.insert(c, id);
    connect(c, &Client::disconnected, this,
        [this] (Client *c) {
            m_clientIds.remove(c);
        }
    );
    return id;
}

bool WaylandServer::isScreenLocked() const
{
    if (!hasScreenLockerIntegration()) {
        return false;
    }
    return ScreenLocker::KSldApp::self()->lockState() == ScreenLocker::KSldApp::Locked ||
           ScreenLocker::KSldApp::self()->lockState() == ScreenLocker::KSldApp::AcquiringLock;
}

bool WaylandServer::hasScreenLockerIntegration() const
{
    return !m_initFlags.testFlag(InitializationFlag::NoLockScreenIntegration);
}

bool WaylandServer::hasGlobalShortcutSupport() const
{
    return !m_initFlags.testFlag(InitializationFlag::NoGlobalShortcuts);
}

void WaylandServer::simulateUserActivity()
{
    if (m_idle) {
        m_idle->simulateUserActivity();
    }
}

void WaylandServer::updateKeyState(input::xkb::LEDs leds)
{
    if (!m_keyState)
        return;

    m_keyState->setState(KeyState::Key::CapsLock,
                         leds & input::xkb::LED::CapsLock ? KeyState::State::Locked
                                                          : KeyState::State::Unlocked);
    m_keyState->setState(KeyState::Key::NumLock,
                         leds & input::xkb::LED::NumLock ? KeyState::State::Locked
                                                         : KeyState::State::Unlocked);
    m_keyState->setState(KeyState::Key::ScrollLock,
                         leds & input::xkb::LED::ScrollLock ? KeyState::State::Locked
                                                            : KeyState::State::Unlocked);
}

}
