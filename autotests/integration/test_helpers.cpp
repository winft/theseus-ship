/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "kwin_wayland_test.h"
#include "screenlockerwatcher.h"
#include "screens.h"
#include "wayland_server.h"

#include "input/backend/wlroots/keyboard.h"
#include "input/backend/wlroots/pointer.h"
#include "input/backend/wlroots/touch.h"
#include "win/wayland/space.h"
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
#include <Wrapland/Client/xdg_activation_v1.h>
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

client::client(global_selection globals)
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

    if (flags(globals & global_selection::seat)) {
        interfaces.seat.reset(
            registry->createSeat(registry->interface(Clt::Registry::Interface::Seat).name,
                                 registry->interface(Clt::Registry::Interface::Seat).version));
        QVERIFY(interfaces.seat->isValid());
    }

    if (flags(globals & global_selection::shadow)) {
        interfaces.shadow_manager.reset(registry->createShadowManager(
            registry->interface(Clt::Registry::Interface::Shadow).name,
            registry->interface(Clt::Registry::Interface::Shadow).version));
        QVERIFY(interfaces.shadow_manager->isValid());
    }

    if (flags(globals & global_selection::plasma_shell)) {
        interfaces.plasma_shell.reset(registry->createPlasmaShell(
            registry->interface(Clt::Registry::Interface::PlasmaShell).name,
            registry->interface(Clt::Registry::Interface::PlasmaShell).version));
        QVERIFY(interfaces.plasma_shell->isValid());
    }

    if (flags(globals & global_selection::window_management)) {
        interfaces.window_management.reset(registry->createPlasmaWindowManagement(
            registry->interface(Clt::Registry::Interface::PlasmaWindowManagement).name,
            registry->interface(Clt::Registry::Interface::PlasmaWindowManagement).version));
        QVERIFY(interfaces.window_management->isValid());
    }

    if (flags(globals & global_selection::pointer_constraints)) {
        interfaces.pointer_constraints.reset(registry->createPointerConstraints(
            registry->interface(Clt::Registry::Interface::PointerConstraintsUnstableV1).name,
            registry->interface(Clt::Registry::Interface::PointerConstraintsUnstableV1).version));
        QVERIFY(interfaces.pointer_constraints->isValid());
    }

    if (flags(globals & global_selection::idle_inhibition)) {
        interfaces.idle_inhibit.reset(registry->createIdleInhibitManager(
            registry->interface(Clt::Registry::Interface::IdleInhibitManagerUnstableV1).name,
            registry->interface(Clt::Registry::Interface::IdleInhibitManagerUnstableV1).version));
        QVERIFY(interfaces.idle_inhibit->isValid());
    }

    if (flags(globals & global_selection::appmenu)) {
        interfaces.app_menu.reset(registry->createAppMenuManager(
            registry->interface(Clt::Registry::Interface::AppMenu).name,
            registry->interface(Clt::Registry::Interface::AppMenu).version));
        QVERIFY(interfaces.app_menu->isValid());
    }

    if (flags(globals & global_selection::xdg_activation)) {
        interfaces.xdg_activation.reset(registry->createXdgActivationV1(
            registry->interface(Clt::Registry::Interface::XdgActivationV1).name,
            registry->interface(Clt::Registry::Interface::XdgActivationV1).version));
        QVERIFY(interfaces.xdg_activation->isValid());
    }

    if (flags(globals & global_selection::xdg_decoration)) {
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

WaylandTestApplication* app()
{
    return static_cast<WaylandTestApplication*>(kwinApp());
}

void setup_wayland_connection(global_selection globals)
{
    get_all_clients().emplace_back(globals);
}

void destroy_wayland_connection()
{
    get_all_clients().clear();
}

client& get_client()
{
    return get_all_clients().front();
}

std::vector<client>& get_all_clients()
{
    return app()->clients;
}

bool wait_for_wayland_pointer()
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

bool wait_for_wayland_touch()
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

bool wait_for_wayland_keyboard()
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
    render(get_client(), surface, size, color, format);
}

void render(client const& clt,
            std::unique_ptr<Clt::Surface> const& surface,
            const QSize& size,
            const QColor& color,
            const QImage::Format& format)
{
    QImage img(size, format);
    img.fill(color);
    render(clt, surface, img);
}

void render(std::unique_ptr<Clt::Surface> const& surface, const QImage& img)
{
    render(get_client(), surface, img);
}

void render(client const& clt, std::unique_ptr<Clt::Surface> const& surface, const QImage& img)
{
    surface->attachBuffer(clt.interfaces.shm->createBuffer(img));
    surface->damage(QRect(QPoint(0, 0), img.size()));
    surface->commit(Clt::Surface::CommitFlag::None);
}

win::wayland::window* render_and_wait_for_shown(std::unique_ptr<Clt::Surface> const& surface,
                                                QSize const& size,
                                                QColor const& color,
                                                QImage::Format const& format,
                                                int timeout)
{
    return render_and_wait_for_shown(get_client(), surface, size, color, format, timeout);
}

win::wayland::window* render_and_wait_for_shown(client const& clt,
                                                std::unique_ptr<Clt::Surface> const& surface,
                                                QSize const& size,
                                                QColor const& color,
                                                QImage::Format const& format,
                                                int timeout)
{
    QSignalSpy clientAddedSpy(static_cast<win::wayland::space*>(workspace()),
                              &win::wayland::space::wayland_window_added);
    if (!clientAddedSpy.isValid()) {
        return nullptr;
    }
    render(clt, surface, size, color, format);
    flush_wayland_connection(clt);
    if (!clientAddedSpy.wait(timeout)) {
        return nullptr;
    }
    return clientAddedSpy.first().first().value<win::wayland::window*>();
}

void flush_wayland_connection()
{
    flush_wayland_connection(get_client());
}

void flush_wayland_connection(client const& clt)
{
    if (clt.connection) {
        clt.connection->flush();
    }
}

std::unique_ptr<Clt::Surface> create_surface()
{
    return create_surface(get_client());
}

std::unique_ptr<Clt::Surface> create_surface(client const& clt)
{
    if (!clt.interfaces.compositor) {
        return nullptr;
    }
    auto surface = std::unique_ptr<Clt::Surface>(clt.interfaces.compositor->createSurface());
    if (!surface->isValid()) {
        return nullptr;
    }
    return surface;
}

std::unique_ptr<Clt::SubSurface>
create_subsurface(std::unique_ptr<Clt::Surface> const& surface,
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
    return create_xdg_shell_toplevel(get_client(), surface, creationSetup);
}

std::unique_ptr<Clt::XdgShellToplevel>
create_xdg_shell_toplevel(client const& clt,
                          std::unique_ptr<Clt::Surface> const& surface,
                          CreationSetup creationSetup)
{
    if (!clt.interfaces.xdg_shell) {
        return nullptr;
    }
    auto toplevel = std::unique_ptr<Clt::XdgShellToplevel>(
        clt.interfaces.xdg_shell->create_toplevel(surface.get()));
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
    return create_xdg_shell_popup(
        get_client(), surface, parent_toplevel, positioner, creationSetup);
}

std::unique_ptr<Clt::XdgShellPopup>
create_xdg_shell_popup(client const& clt,
                       std::unique_ptr<Clt::Surface> const& surface,
                       std::unique_ptr<Clt::XdgShellToplevel> const& parent_toplevel,
                       Clt::XdgPositioner const& positioner,
                       CreationSetup creationSetup)
{
    if (!clt.interfaces.xdg_shell) {
        return nullptr;
    }
    auto popup = std::unique_ptr<Clt::XdgShellPopup>(
        clt.interfaces.xdg_shell->create_popup(surface.get(), parent_toplevel.get(), positioner));
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

bool wait_for_destroyed(Toplevel* window)
{
    QSignalSpy destroyedSpy(window, &QObject::destroyed);
    if (!destroyedSpy.isValid()) {
        return false;
    }
    return destroyedSpy.wait();
}

void lock_screen()
{
    QVERIFY(!kwinApp()->is_screen_locked());

    QSignalSpy lockStateChangedSpy(ScreenLocker::KSldApp::self(),
                                   &ScreenLocker::KSldApp::lockStateChanged);
    QVERIFY(lockStateChangedSpy.isValid());
    QSignalSpy lockWatcherSpy(ScreenLockerWatcher::self(), &ScreenLockerWatcher::locked);
    QVERIFY(lockWatcherSpy.isValid());

    ScreenLocker::KSldApp::self()->lock(ScreenLocker::EstablishLock::Immediate);
    QCOMPARE(lockStateChangedSpy.count(), 1);

    QVERIFY(kwinApp()->is_screen_locked());
    QVERIFY(lockWatcherSpy.wait());
    QCOMPARE(lockWatcherSpy.count(), 1);
    QCOMPARE(lockStateChangedSpy.count(), 2);

    QVERIFY(ScreenLockerWatcher::self()->isLocked());
}

void unlock_screen()
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

    QVERIFY(!kwinApp()->is_screen_locked());

    QVERIFY(!ScreenLockerWatcher::self()->isLocked());
}

void prepare_app_env(std::string const& qpa_plugin_path)
{
    QStandardPaths::setTestModeEnabled(true);

    setenv("QT_QPA_PLATFORM", "wayland-org.kde.kwin.qpa", true);
    setenv(
        "QT_QPA_PLATFORM_PLUGIN_PATH",
        QFileInfo(QString::fromStdString(qpa_plugin_path)).absolutePath().toLocal8Bit().constData(),
        true);
    setenv("KWIN_FORCE_OWN_QPA", "1", true);
    setenv("XDG_CURRENT_DESKTOP", "KDE", true);
    setenv("KWIN_WLR_OUTPUT_ALIGN_HORIZONTAL", "0", true);

    unsetenv("KDE_FULL_SESSION");
    unsetenv("KDE_SESSION_VERSION");
    unsetenv("XDG_SESSION_DESKTOP");

    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QCoreApplication::setAttribute(Qt::AA_Use96Dpi, true);
}

void prepare_sys_env(std::string const& socket_name)
{
    // Reset QT_QPA_PLATFORM for any other processes started.
    setenv("QT_QPA_PLATFORM", "wayland", true);
    setenv("WAYLAND_DISPLAY", socket_name.c_str(), true);
}

std::string create_socket_name(std::string base)
{
    base.erase(std::remove_if(base.begin(), base.end(), [](char c) { return !isalpha(c); }),
               base.end());
    std::transform(base.begin(), base.end(), base.begin(), [](char c) { return std::tolower(c); });
    return "wayland_" + base + "-0";
}

//
// From wlroots util/signal.c, not (yet) part of wlroots's public API.
void handle_noop([[maybe_unused]] wl_listener* listener, [[maybe_unused]] void* data)
{
    // Do nothing
}

void wlr_signal_emit_safe(wl_signal* signal, void* data)
{
    wl_listener cursor;
    wl_listener end;

    /* Add two special markers: one cursor and one end marker. This way, we know
     * that we've already called listeners on the left of the cursor and that we
     * don't want to call listeners on the right of the end marker. The 'it'
     * function can remove any element it wants from the list without troubles.
     * wl_list_for_each_safe tries to be safe but it fails: it works fine
     * if the current item is removed, but not if the next one is. */
    wl_list_insert(&signal->listener_list, &cursor.link);
    cursor.notify = handle_noop;
    wl_list_insert(signal->listener_list.prev, &end.link);
    end.notify = handle_noop;

    while (cursor.link.next != &end.link) {
        wl_list* pos = cursor.link.next;
        wl_listener* l = wl_container_of(pos, l, link);

        wl_list_remove(&cursor.link);
        wl_list_insert(pos, &cursor.link);

        l->notify(l, data);
    }

    wl_list_remove(&cursor.link);
    wl_list_remove(&end.link);
}
//
//

void pointer_motion_absolute(QPointF const& position, uint32_t time)
{
    auto app = Test::app();

    QVERIFY(app->pointer);

    wlr_event_pointer_motion_absolute event{};

    event.device = app->pointer;
    event.time_msec = time;

    auto const screens_size = screens()->size();
    event.x = position.x() / screens_size.width();
    event.y = position.y() / screens_size.height();

    wlr_signal_emit_safe(&app->pointer->pointer->events.motion_absolute, &event);
    wlr_signal_emit_safe(&app->pointer->pointer->events.frame, app->pointer->pointer);
}

void pointer_button_impl(uint32_t button, uint32_t time, wlr_button_state state)
{
    auto app = Test::app();

    QVERIFY(app->pointer);

    wlr_event_pointer_button event{};

    event.device = app->pointer;
    event.time_msec = time;

    event.button = button;
    event.state = state;

    wlr_signal_emit_safe(&app->pointer->pointer->events.button, &event);
    wlr_signal_emit_safe(&app->pointer->pointer->events.frame, app->pointer->pointer);
}

void pointer_button_pressed(uint32_t button, uint32_t time)
{
    pointer_button_impl(button, time, WLR_BUTTON_PRESSED);
}

void pointer_button_released(uint32_t button, uint32_t time)
{
    pointer_button_impl(button, time, WLR_BUTTON_RELEASED);
}

void pointer_axis_impl(double delta,
                       uint32_t time,
                       int32_t discrete_delta,
                       wlr_axis_orientation orientation,
                       wlr_axis_source source)
{
    auto app = Test::app();

    QVERIFY(app->pointer);

    wlr_event_pointer_axis event{};

    event.device = app->pointer;
    event.time_msec = time;

    event.delta = delta;
    event.delta_discrete = discrete_delta;
    event.orientation = orientation;
    event.source = source;

    wlr_signal_emit_safe(&app->pointer->pointer->events.axis, &event);
    wlr_signal_emit_safe(&app->pointer->pointer->events.frame, app->pointer->pointer);
}

void pointer_axis_horizontal(double delta, uint32_t time, int32_t discrete_delta)
{
    pointer_axis_impl(
        delta, time, discrete_delta, WLR_AXIS_ORIENTATION_HORIZONTAL, WLR_AXIS_SOURCE_WHEEL);
}

void pointer_axis_vertical(double delta, uint32_t time, int32_t discrete_delta)
{
    pointer_axis_impl(
        delta, time, discrete_delta, WLR_AXIS_ORIENTATION_VERTICAL, WLR_AXIS_SOURCE_WHEEL);
}

void keyboard_key_impl(uint32_t key, uint32_t time, bool update_state, wl_keyboard_key_state state)
{
    auto app = Test::app();

    QVERIFY(app->keyboard);

    wlr_event_keyboard_key event{};

    event.keycode = key;
    event.time_msec = time;
    event.update_state = update_state;
    event.state = state;

    wlr_signal_emit_safe(&app->keyboard->keyboard->events.key, &event);
}

void keyboard_key_pressed(uint32_t key, uint32_t time)
{
    keyboard_key_impl(key, time, true, WL_KEYBOARD_KEY_STATE_PRESSED);
}

void keyboard_key_released(uint32_t key, uint32_t time)
{
    keyboard_key_impl(key, time, true, WL_KEYBOARD_KEY_STATE_RELEASED);
}

QPointF get_relative_touch_position(QPointF const& pos)
{
    auto screen_number = screens()->number(pos.toPoint());
    auto output_size = screens()->size(screen_number);

    return QPointF(pos.x() / output_size.width(), pos.y() / output_size.height());
}

void touch_down(int32_t id, QPointF const& position, uint32_t time)
{
    auto app = Test::app();

    QVERIFY(app->touch);

    wlr_event_touch_down event{};

    event.device = app->touch;
    event.time_msec = time;

    event.touch_id = id;

    auto rel_pos = get_relative_touch_position(position);
    event.x = rel_pos.x();
    event.y = rel_pos.y();

    wlr_signal_emit_safe(&app->touch->touch->events.down, &event);
}

void touch_up(int32_t id, uint32_t time)
{
    auto app = Test::app();

    QVERIFY(app->touch);

    wlr_event_touch_up event{};

    event.device = app->touch;
    event.time_msec = time;

    event.touch_id = id;

    wlr_signal_emit_safe(&app->touch->touch->events.up, &event);
}

void touch_motion(int32_t id, QPointF const& position, uint32_t time)
{
    auto app = Test::app();

    QVERIFY(app->touch);

    wlr_event_touch_motion event{};

    event.device = app->touch;
    event.time_msec = time;

    event.touch_id = id;

    auto rel_pos = get_relative_touch_position(position);
    event.x = rel_pos.x();
    event.y = rel_pos.y();

    wlr_signal_emit_safe(&app->touch->touch->events.motion, &event);
}

void touch_cancel()
{
    auto app = Test::app();

    QVERIFY(app->touch);

    wlr_event_touch_cancel event{};

    event.device = app->touch;

    wlr_signal_emit_safe(&app->touch->touch->events.cancel, &event);
}

}
