/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "kwin_wayland_test.h"
#include "screenlockerwatcher.h"
#include "wayland_server.h"

#include "win/wayland/window.h"

#include <Wrapland/Client/appmenu.h>
#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/event_queue.h>
#include <Wrapland/Client/idleinhibit.h>
#include <Wrapland/Client/layer_shell_v1.h>
#include <Wrapland/Client/output.h>
#include <Wrapland/Client/plasmashell.h>
#include <Wrapland/Client/plasmawindowmanagement.h>
#include <Wrapland/Client/pointerconstraints.h>
#include <Wrapland/Client/registry.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/shadow.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/subcompositor.h>
#include <Wrapland/Client/subsurface.h>
#include <Wrapland/Client/surface.h>
#include <Wrapland/Client/xdg_shell.h>
#include <Wrapland/Client/xdgdecoration.h>
#include <Wrapland/Server/display.h>

#include <KScreenLocker/KsldApp>

#include <QThread>

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace Clt = Wrapland::Client;

namespace KWin::Test
{

client::client(AdditionalWaylandInterfaces flags)
{
    int sx[2];
    QVERIFY(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sx) >= 0);

    KWin::waylandServer()->display()->createClient(sx[0]);

    // Setup connection.
    connection = new Clt::ConnectionThread;

    QSignalSpy connectedSpy(connection, &Clt::ConnectionThread::establishedChanged);
    QVERIFY(connectedSpy.isValid());

    connection->setSocketFd(sx[1]);

    thread.reset(new QThread(kwinApp()));
    connection->moveToThread(thread.get());
    thread->start();

    connection->establishConnection();
    QVERIFY(connectedSpy.count() || connectedSpy.wait());
    QCOMPARE(connectedSpy.count(), 1);
    QVERIFY(connection->established());

    queue.reset(new Clt::EventQueue);
    queue->setup(connection);
    QVERIFY(queue->isValid());

    registry.reset(new Clt::Registry);
    registry->setEventQueue(queue.get());

    connect_outputs();

    QSignalSpy allAnnounced(registry.get(), &Clt::Registry::interfacesAnnounced);
    QVERIFY(allAnnounced.isValid());

    registry->create(connection);
    QVERIFY(registry->isValid());

    registry->setup();
    QVERIFY(allAnnounced.count() || allAnnounced.wait());
    QCOMPARE(allAnnounced.count(), 1);

    interfaces.compositor.reset(registry->createCompositor(
        registry->interface(Clt::Registry::Interface::Compositor).name,
        registry->interface(Clt::Registry::Interface::Compositor).version));
    QVERIFY(interfaces.compositor->isValid());

    interfaces.subcompositor.reset(registry->createSubCompositor(
        registry->interface(Clt::Registry::Interface::SubCompositor).name,
        registry->interface(Clt::Registry::Interface::SubCompositor).version));
    QVERIFY(interfaces.subcompositor->isValid());

    interfaces.shm.reset(
        registry->createShmPool(registry->interface(Clt::Registry::Interface::Shm).name,
                                registry->interface(Clt::Registry::Interface::Shm).version));
    QVERIFY(interfaces.shm->isValid());

    interfaces.xdg_shell.reset(
        registry->createXdgShell(registry->interface(Clt::Registry::Interface::XdgShell).name,
                                 registry->interface(Clt::Registry::Interface::XdgShell).version));
    QVERIFY(interfaces.xdg_shell->isValid());

    interfaces.layer_shell.reset(registry->createLayerShellV1(
        registry->interface(Clt::Registry::Interface::LayerShellV1).name,
        registry->interface(Clt::Registry::Interface::LayerShellV1).version));
    QVERIFY(interfaces.layer_shell->isValid());

    if (flags.testFlag(AdditionalWaylandInterface::Seat)) {
        interfaces.seat.reset(
            registry->createSeat(registry->interface(Clt::Registry::Interface::Seat).name,
                                 registry->interface(Clt::Registry::Interface::Seat).version));
        QVERIFY(interfaces.seat->isValid());
    }

    if (flags.testFlag(AdditionalWaylandInterface::ShadowManager)) {
        interfaces.shadow_manager.reset(registry->createShadowManager(
            registry->interface(Clt::Registry::Interface::Shadow).name,
            registry->interface(Clt::Registry::Interface::Shadow).version));
        QVERIFY(interfaces.shadow_manager->isValid());
    }

    if (flags.testFlag(AdditionalWaylandInterface::PlasmaShell)) {
        interfaces.plasma_shell.reset(registry->createPlasmaShell(
            registry->interface(Clt::Registry::Interface::PlasmaShell).name,
            registry->interface(Clt::Registry::Interface::PlasmaShell).version));
        QVERIFY(interfaces.plasma_shell->isValid());
    }

    if (flags.testFlag(AdditionalWaylandInterface::WindowManagement)) {
        interfaces.window_management.reset(registry->createPlasmaWindowManagement(
            registry->interface(Clt::Registry::Interface::PlasmaWindowManagement).name,
            registry->interface(Clt::Registry::Interface::PlasmaWindowManagement).version));
        QVERIFY(interfaces.window_management->isValid());
    }

    if (flags.testFlag(AdditionalWaylandInterface::PointerConstraints)) {
        interfaces.pointer_constraints.reset(registry->createPointerConstraints(
            registry->interface(Clt::Registry::Interface::PointerConstraintsUnstableV1).name,
            registry->interface(Clt::Registry::Interface::PointerConstraintsUnstableV1).version));
        QVERIFY(interfaces.pointer_constraints->isValid());
    }

    if (flags.testFlag(AdditionalWaylandInterface::IdleInhibition)) {
        interfaces.idle_inhibit.reset(registry->createIdleInhibitManager(
            registry->interface(Clt::Registry::Interface::IdleInhibitManagerUnstableV1).name,
            registry->interface(Clt::Registry::Interface::IdleInhibitManagerUnstableV1).version));
        QVERIFY(interfaces.idle_inhibit->isValid());
    }

    if (flags.testFlag(AdditionalWaylandInterface::AppMenu)) {
        interfaces.app_menu.reset(registry->createAppMenuManager(
            registry->interface(Clt::Registry::Interface::AppMenu).name,
            registry->interface(Clt::Registry::Interface::AppMenu).version));
        QVERIFY(interfaces.app_menu->isValid());
    }

    if (flags.testFlag(AdditionalWaylandInterface::XdgDecoration)) {
        interfaces.xdg_decoration.reset(registry->createXdgDecorationManager(
            registry->interface(Clt::Registry::Interface::XdgDecorationUnstableV1).name,
            registry->interface(Clt::Registry::Interface::XdgDecorationUnstableV1).version));
        QVERIFY(interfaces.xdg_decoration->isValid());
    }
}

client::client(client&& other) noexcept
{
    *this = std::move(other);
}

client& client::operator=(client&& other) noexcept
{
    cleanup();

    QObject::disconnect(other.output_announced);
    for (auto& con : other.output_removals) {
        QObject::disconnect(con);
    }

    connection = other.connection;
    other.connection = nullptr;

    thread = std::move(other.thread);
    queue = std::move(other.queue);
    registry = std::move(other.registry);
    interfaces = std::move(other.interfaces);

    connect_outputs();

    return *this;
}

client::~client()
{
    cleanup();
}

void client::connect_outputs()
{
    output_announced = QObject::connect(
        registry.get(), &Clt::Registry::outputAnnounced, [&](quint32 name, quint32 version) {
            auto output = std::unique_ptr<Clt::Output>(
                registry->createOutput(name, version, registry.get()));
            output_removals.push_back(output_removal_connection(output.get()));
            interfaces.outputs.push_back(std::move(output));
        });
    for (auto& output : interfaces.outputs) {
        output_removals.push_back(output_removal_connection(output.get()));
    }
}

QMetaObject::Connection client::output_removal_connection(Wrapland::Client::Output* output)
{
    return QObject::connect(output, &Clt::Output::removed, [output, this]() {
        output->deleteLater();
        auto& outs = interfaces.outputs;
        outs.erase(std::remove_if(outs.begin(),
                                  outs.end(),
                                  [output](auto const& out) { return out.get() == output; }),
                   outs.end());
    });
}

void client::cleanup()
{
    if (!connection) {
        return;
    }
    interfaces = {};
    registry.reset();
    queue.reset();

    if (thread) {
        QSignalSpy spy(connection, &QObject::destroyed);
        QVERIFY(spy.isValid());

        connection->deleteLater();
        QVERIFY(!spy.isEmpty() || spy.wait());
        QCOMPARE(spy.count(), 1);

        thread->quit();
        thread->wait();
        thread.reset();
        connection = nullptr;
    } else {
        delete connection;
        connection = nullptr;
    }
}

void setupWaylandConnection(AdditionalWaylandInterfaces flags)
{
    QVERIFY(get_all_clients().empty());
    get_all_clients().emplace_back(flags);
}

void destroyWaylandConnection()
{
    get_all_clients().clear();
}

client& get_client()
{
    return get_all_clients().front();
}

std::vector<client>& get_all_clients()
{
    auto app = static_cast<WaylandTestApplication*>(kwinApp());
    return app->clients;
}

bool waitForWaylandPointer()
{
    if (!get_client().interfaces.seat) {
        return false;
    }
    QSignalSpy hasPointerSpy(get_client().interfaces.seat.get(), &Clt::Seat::hasPointerChanged);
    if (!hasPointerSpy.isValid()) {
        return false;
    }
    return hasPointerSpy.wait();
}

bool waitForWaylandTouch()
{
    if (!get_client().interfaces.seat) {
        return false;
    }
    QSignalSpy hasTouchSpy(get_client().interfaces.seat.get(), &Clt::Seat::hasTouchChanged);
    if (!hasTouchSpy.isValid()) {
        return false;
    }
    return hasTouchSpy.wait();
}

bool waitForWaylandKeyboard()
{
    if (!get_client().interfaces.seat) {
        return false;
    }
    QSignalSpy hasKeyboardSpy(get_client().interfaces.seat.get(), &Clt::Seat::hasKeyboardChanged);
    if (!hasKeyboardSpy.isValid()) {
        return false;
    }
    return hasKeyboardSpy.wait();
}

void render(std::unique_ptr<Clt::Surface> const& surface,
            const QSize& size,
            const QColor& color,
            const QImage::Format& format)
{
    QImage img(size, format);
    img.fill(color);
    render(surface, img);
}

void render(std::unique_ptr<Clt::Surface> const& surface, const QImage& img)
{
    surface->attachBuffer(get_client().interfaces.shm->createBuffer(img));
    surface->damage(QRect(QPoint(0, 0), img.size()));
    surface->commit(Clt::Surface::CommitFlag::None);
}

win::wayland::window* waitForWaylandWindowShown(int timeout)
{
    QSignalSpy clientAddedSpy(waylandServer(), &WaylandServer::window_added);
    if (!clientAddedSpy.isValid()) {
        return nullptr;
    }
    if (!clientAddedSpy.wait(timeout)) {
        return nullptr;
    }
    return clientAddedSpy.first().first().value<win::wayland::window*>();
}

win::wayland::window* renderAndWaitForShown(std::unique_ptr<Clt::Surface> const& surface,
                                            const QSize& size,
                                            const QColor& color,
                                            const QImage::Format& format,
                                            int timeout)
{
    QSignalSpy clientAddedSpy(waylandServer(), &WaylandServer::window_added);
    if (!clientAddedSpy.isValid()) {
        return nullptr;
    }
    render(surface, size, color, format);
    flushWaylandConnection();
    if (!clientAddedSpy.wait(timeout)) {
        return nullptr;
    }
    return clientAddedSpy.first().first().value<win::wayland::window*>();
}

void flushWaylandConnection()
{
    if (get_client().connection) {
        get_client().connection->flush();
    }
}

std::unique_ptr<Clt::Surface> createSurface()
{
    if (!get_client().interfaces.compositor) {
        return nullptr;
    }
    auto surface
        = std::unique_ptr<Clt::Surface>(get_client().interfaces.compositor->createSurface());
    if (!surface->isValid()) {
        return nullptr;
    }
    return surface;
}

std::unique_ptr<Clt::SubSurface>
createSubSurface(std::unique_ptr<Clt::Surface> const& surface,
                 std::unique_ptr<Clt::Surface> const& parent_surface)
{
    if (!get_client().interfaces.subcompositor) {
        return nullptr;
    }
    auto subsurface
        = std::unique_ptr<Clt::SubSurface>(get_client().interfaces.subcompositor->createSubSurface(
            surface.get(), parent_surface.get()));
    if (!subsurface->isValid()) {
        return nullptr;
    }
    return subsurface;
}

std::unique_ptr<Clt::XdgShellToplevel>
create_xdg_shell_toplevel(std::unique_ptr<Clt::Surface> const& surface, CreationSetup creationSetup)
{
    if (!get_client().interfaces.xdg_shell) {
        return nullptr;
    }
    auto toplevel = std::unique_ptr<Clt::XdgShellToplevel>(
        get_client().interfaces.xdg_shell->create_toplevel(surface.get()));
    if (!toplevel->isValid()) {
        return nullptr;
    }
    if (creationSetup == CreationSetup::CreateAndConfigure) {
        init_xdg_shell_toplevel(surface, toplevel);
    }
    return toplevel;
}

std::unique_ptr<Clt::XdgShellPopup>
create_xdg_shell_popup(std::unique_ptr<Clt::Surface> const& surface,
                       std::unique_ptr<Clt::XdgShellToplevel> const& parent_toplevel,
                       Clt::XdgPositioner const& positioner,
                       CreationSetup creationSetup)
{
    if (!get_client().interfaces.xdg_shell) {
        return nullptr;
    }
    auto popup
        = std::unique_ptr<Clt::XdgShellPopup>(get_client().interfaces.xdg_shell->create_popup(
            surface.get(), parent_toplevel.get(), positioner));
    if (!popup->isValid()) {
        return nullptr;
    }
    if (creationSetup == CreationSetup::CreateAndConfigure) {
        init_xdg_shell_popup(surface, popup);
    }
    return popup;
}

void init_xdg_shell_toplevel(std::unique_ptr<Clt::Surface> const& surface,
                             std::unique_ptr<Clt::XdgShellToplevel> const& shell_toplevel)
{
    // wait for configure
    QSignalSpy configureRequestedSpy(shell_toplevel.get(),
                                     &Clt::XdgShellToplevel::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    surface->commit(Clt::Surface::CommitFlag::None);
    QVERIFY(configureRequestedSpy.wait());
    shell_toplevel->ackConfigure(configureRequestedSpy.last()[2].toInt());
}

void init_xdg_shell_popup(std::unique_ptr<Clt::Surface> const& surface,
                          std::unique_ptr<Clt::XdgShellPopup> const& popup)
{
    // wait for configure
    QSignalSpy configureRequestedSpy(popup.get(), &Clt::XdgShellPopup::configureRequested);
    QVERIFY(configureRequestedSpy.isValid());
    surface->commit(Clt::Surface::CommitFlag::None);
    QVERIFY(configureRequestedSpy.wait());
    popup->ackConfigure(configureRequestedSpy.last()[1].toInt());
}

bool waitForWindowDestroyed(Toplevel* window)
{
    QSignalSpy destroyedSpy(window, &QObject::destroyed);
    if (!destroyedSpy.isValid()) {
        return false;
    }
    return destroyedSpy.wait();
}

void lockScreen()
{
    QVERIFY(!waylandServer()->isScreenLocked());

    QSignalSpy lockStateChangedSpy(ScreenLocker::KSldApp::self(),
                                   &ScreenLocker::KSldApp::lockStateChanged);
    QVERIFY(lockStateChangedSpy.isValid());
    QSignalSpy lockWatcherSpy(ScreenLockerWatcher::self(), &ScreenLockerWatcher::locked);
    QVERIFY(lockWatcherSpy.isValid());

    ScreenLocker::KSldApp::self()->lock(ScreenLocker::EstablishLock::Immediate);
    QCOMPARE(lockStateChangedSpy.count(), 1);

    QVERIFY(waylandServer()->isScreenLocked());
    QVERIFY(lockWatcherSpy.wait());
    QCOMPARE(lockWatcherSpy.count(), 1);
    QCOMPARE(lockStateChangedSpy.count(), 2);

    QVERIFY(ScreenLockerWatcher::self()->isLocked());
}

void unlockScreen()
{
    QSignalSpy lockStateChangedSpy(ScreenLocker::KSldApp::self(),
                                   &ScreenLocker::KSldApp::lockStateChanged);
    QVERIFY(lockStateChangedSpy.isValid());
    QSignalSpy lockWatcherSpy(ScreenLockerWatcher::self(), &ScreenLockerWatcher::locked);
    QVERIFY(lockWatcherSpy.isValid());

    auto const children = ScreenLocker::KSldApp::self()->children();
    auto logind_integration_found{false};
    for (auto it = children.begin(); it != children.end(); ++it) {
        if (qstrcmp((*it)->metaObject()->className(), "LogindIntegration") == 0) {
            logind_integration_found = true;

            // KScreenLocker does not handle unlock requests via logind reliable as it sends a
            // SIGTERM signal to the lock process which sometimes under high system load is not
            // received and handled by the process.
            // It is unclear why the signal is never received but we can repeat sending the signal
            // mutliple times (here 10) assuming that after few tries one of them will be received.
            int unlock_tries = 0;
            while (unlock_tries++ < 10) {
                QMetaObject::invokeMethod(*it, "requestUnlock");
                lockWatcherSpy.wait(1000);
                if (lockWatcherSpy.count()) {
                    break;
                }
            }

            break;
        }
    }
    QVERIFY(logind_integration_found);
    QCOMPARE(lockWatcherSpy.count(), 1);
    QCOMPARE(lockStateChangedSpy.count(), 1);

    QVERIFY(!waylandServer()->isScreenLocked());

    QVERIFY(!ScreenLockerWatcher::self()->isLocked());
}

}
