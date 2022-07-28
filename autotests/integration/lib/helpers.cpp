/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "helpers.h"

#include "app.h"

#include "base/output_helpers.h"
#include "desktop/screen_locker_watcher.h"
#include "input/backend/wlroots/keyboard.h"
#include "input/backend/wlroots/pointer.h"
#include "input/backend/wlroots/touch.h"
#include "win/wayland/space.h"
#include "win/wayland/window.h"

#include <KScreenLocker/KsldApp>
#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/connection_thread.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/subsurface.h>
#include <Wrapland/Client/surface.h>

namespace Clt = Wrapland::Client;

namespace KWin::Test
{

output::output(QRect const& geometry)
    : output(geometry, 1.)
{
}

output::output(QRect const& geometry, double scale)
    : geometry{geometry}
    , scale{scale}
{
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

base::output* get_output(size_t index)
{
    auto const& outputs = Test::app()->base.get_outputs();
    assert(index < outputs.size());
    return outputs.at(index);
}

void set_current_output(int index)
{
    auto const& outputs = Test::app()->base.get_outputs();
    auto output = base::get_output(outputs, index);
    QVERIFY(output);
    base::set_current_output(Test::app()->base, output);
}

void test_outputs_default()
{
    test_outputs_geometries({QRect(0, 0, 1280, 1024), QRect(1280, 0, 1280, 1024)});
}

void test_outputs_geometries(std::vector<QRect> const& geometries)
{
    auto const& outputs = Test::app()->base.get_outputs();
    QCOMPARE(outputs.size(), geometries.size());

    size_t index = 0;
    for (auto geo : geometries) {
        QCOMPARE(outputs.at(index)->geometry(), geo);
        index++;
    }
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
    QSignalSpy clientAddedSpy(app()->base.space->qobject.get(),
                              &win::space::qobject_t::wayland_window_added);
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

    std::unique_ptr<Clt::XdgShellPopup> popup;
    if (parent_toplevel) {
        popup.reset(clt.interfaces.xdg_shell->create_popup(
            surface.get(), parent_toplevel.get(), positioner));
    } else {
        popup.reset(clt.interfaces.xdg_shell->create_popup(surface.get(), positioner));
    }

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
    QSignalSpy lockWatcherSpy(kwinApp()->screen_locker_watcher.get(),
                              &desktop::screen_locker_watcher::locked);
    QVERIFY(lockWatcherSpy.isValid());

    ScreenLocker::KSldApp::self()->lock(ScreenLocker::EstablishLock::Immediate);
    QCOMPARE(lockStateChangedSpy.count(), 1);

    QVERIFY(kwinApp()->is_screen_locked());
    QVERIFY(lockWatcherSpy.wait());
    QCOMPARE(lockWatcherSpy.count(), 1);
    QCOMPARE(lockStateChangedSpy.count(), 2);

    QVERIFY(kwinApp()->screen_locker_watcher->is_locked());
}

void unlock_screen()
{
    QSignalSpy lockStateChangedSpy(ScreenLocker::KSldApp::self(),
                                   &ScreenLocker::KSldApp::lockStateChanged);
    QVERIFY(lockStateChangedSpy.isValid());
    QSignalSpy lockWatcherSpy(kwinApp()->screen_locker_watcher.get(),
                              &desktop::screen_locker_watcher::locked);
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

    QVERIFY(!kwinApp()->screen_locker_watcher->is_locked());
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

    // Run tests by default with QPainter. Individual tests may override when they require GL.
    setenv("KWIN_COMPOSE", "Q", true);

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
#if HAVE_WLR_BASE_INPUT_DEVICES
    wlr_pointer_motion_absolute_event event{};
    event.pointer = app->pointer;
#else
    wlr_event_pointer_motion_absolute event{};
    event.device = app->pointer;
#endif

    event.time_msec = time;

    auto const screens_size = kwinApp()->get_base().topology.size;
    event.x = position.x() / screens_size.width();
    event.y = position.y() / screens_size.height();

#if HAVE_WLR_BASE_INPUT_DEVICES
    wlr_signal_emit_safe(&app->pointer->events.motion_absolute, &event);
    wlr_signal_emit_safe(&app->pointer->events.frame, app->pointer);
#else
    wlr_signal_emit_safe(&app->pointer->pointer->events.motion_absolute, &event);
    wlr_signal_emit_safe(&app->pointer->pointer->events.frame, app->pointer->pointer);
#endif
}

void pointer_button_impl(uint32_t button, uint32_t time, wlr_button_state state)
{
    auto app = Test::app();

    QVERIFY(app->pointer);
#if HAVE_WLR_BASE_INPUT_DEVICES
    wlr_pointer_button_event event{};
    event.pointer = app->pointer;
#else
    wlr_event_pointer_button event{};
    event.device = app->pointer;
#endif

    event.time_msec = time;

    event.button = button;
    event.state = state;

#if HAVE_WLR_BASE_INPUT_DEVICES
    wlr_signal_emit_safe(&app->pointer->events.button, &event);
    wlr_signal_emit_safe(&app->pointer->events.frame, app->pointer);
#else
    wlr_signal_emit_safe(&app->pointer->pointer->events.button, &event);
    wlr_signal_emit_safe(&app->pointer->pointer->events.frame, app->pointer->pointer);
#endif
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
#if HAVE_WLR_BASE_INPUT_DEVICES
    wlr_pointer_axis_event event{};
    event.pointer = app->pointer;
#else
    wlr_event_pointer_axis event{};
    event.device = app->pointer;
#endif

    event.time_msec = time;

    event.delta = delta;
    event.delta_discrete = discrete_delta;
    event.orientation = orientation;
    event.source = source;

#if HAVE_WLR_BASE_INPUT_DEVICES
    wlr_signal_emit_safe(&app->pointer->events.axis, &event);
    wlr_signal_emit_safe(&app->pointer->events.frame, app->pointer);
#else
    wlr_signal_emit_safe(&app->pointer->pointer->events.axis, &event);
    wlr_signal_emit_safe(&app->pointer->pointer->events.frame, app->pointer->pointer);
#endif
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

#if HAVE_WLR_BASE_INPUT_DEVICES
void keyboard_key_impl(uint32_t key,
                       uint32_t time,
                       bool update_state,
                       wl_keyboard_key_state state,
                       wlr_keyboard* keyboard)
{
    wlr_keyboard_key_event event{};

    event.keycode = key;
    event.time_msec = time;
    event.update_state = update_state;
    event.state = state;

    wlr_signal_emit_safe(&keyboard->events.key, &event);
}
#else
void keyboard_key_impl(uint32_t key,
                       uint32_t time,
                       bool update_state,
                       wl_keyboard_key_state state,
                       wlr_input_device* keyboard)
{
    wlr_event_keyboard_key event{};

    event.keycode = key;
    event.time_msec = time;
    event.update_state = update_state;
    event.state = state;

    wlr_signal_emit_safe(&keyboard->keyboard->events.key, &event);
}
#endif

void keyboard_key_pressed(uint32_t key, uint32_t time)
{
    keyboard_key_impl(key, time, true, WL_KEYBOARD_KEY_STATE_PRESSED, Test::app()->keyboard);
}

void keyboard_key_released(uint32_t key, uint32_t time)
{
    keyboard_key_impl(key, time, true, WL_KEYBOARD_KEY_STATE_RELEASED, Test::app()->keyboard);
}

#if HAVE_WLR_BASE_INPUT_DEVICES
void keyboard_key_pressed(uint32_t key, uint32_t time, wlr_keyboard* keyboard)
{
    keyboard_key_impl(key, time, true, WL_KEYBOARD_KEY_STATE_PRESSED, keyboard);
}

KWIN_EXPORT void keyboard_key_released(uint32_t key, uint32_t time, wlr_keyboard* keyboard)
{
    keyboard_key_impl(key, time, true, WL_KEYBOARD_KEY_STATE_RELEASED, keyboard);
}
#else
void keyboard_key_pressed(uint32_t key, uint32_t time, wlr_input_device* keyboard)
{
    keyboard_key_impl(key, time, true, WL_KEYBOARD_KEY_STATE_PRESSED, keyboard);
}

KWIN_EXPORT void keyboard_key_released(uint32_t key, uint32_t time, wlr_input_device* keyboard)
{
    keyboard_key_impl(key, time, true, WL_KEYBOARD_KEY_STATE_RELEASED, keyboard);
}
#endif

QPointF get_relative_touch_position(QPointF const& pos)
{
    auto output = base::get_nearest_output(kwinApp()->get_base().get_outputs(), pos.toPoint());
    assert(output);

    auto output_size = output->geometry().size();
    return QPointF(pos.x() / output_size.width(), pos.y() / output_size.height());
}

void touch_down(int32_t id, QPointF const& position, uint32_t time)
{
    auto app = Test::app();

    QVERIFY(app->touch);
#if HAVE_WLR_BASE_INPUT_DEVICES
    wlr_touch_down_event event{};
    event.touch = app->touch;
#else
    wlr_event_touch_down event{};
    event.device = app->touch;
#endif

    event.time_msec = time;

    event.touch_id = id;

    auto rel_pos = get_relative_touch_position(position);
    event.x = rel_pos.x();
    event.y = rel_pos.y();

#if HAVE_WLR_BASE_INPUT_DEVICES
    wlr_signal_emit_safe(&app->touch->events.down, &event);
#else
    wlr_signal_emit_safe(&app->touch->touch->events.down, &event);
#endif
}

void touch_up(int32_t id, uint32_t time)
{
    auto app = Test::app();

    QVERIFY(app->touch);
#if HAVE_WLR_BASE_INPUT_DEVICES
    wlr_touch_up_event event{};
    event.touch = app->touch;
#else
    wlr_event_touch_up event{};
    event.device = app->touch;
#endif

    event.time_msec = time;

    event.touch_id = id;

#if HAVE_WLR_BASE_INPUT_DEVICES
    wlr_signal_emit_safe(&app->touch->events.up, &event);
#else
    wlr_signal_emit_safe(&app->touch->touch->events.up, &event);
#endif
}

void touch_motion(int32_t id, QPointF const& position, uint32_t time)
{
    auto app = Test::app();

    QVERIFY(app->touch);
#if HAVE_WLR_BASE_INPUT_DEVICES
    wlr_touch_motion_event event{};
    event.touch = app->touch;
#else
    wlr_event_touch_motion event{};
    event.device = app->touch;
#endif

    event.time_msec = time;

    event.touch_id = id;

    auto rel_pos = get_relative_touch_position(position);
    event.x = rel_pos.x();
    event.y = rel_pos.y();

#if HAVE_WLR_BASE_INPUT_DEVICES
    wlr_signal_emit_safe(&app->touch->events.motion, &event);
#else
    wlr_signal_emit_safe(&app->touch->touch->events.motion, &event);
#endif
}

void touch_cancel()
{
    auto app = Test::app();

    QVERIFY(app->touch);
#if HAVE_WLR_BASE_INPUT_DEVICES
    wlr_touch_cancel_event event{};
    event.touch = app->touch;

    wlr_signal_emit_safe(&app->touch->events.cancel, &event);
#else
    wlr_event_touch_cancel event{};
    event.device = app->touch;

    wlr_signal_emit_safe(&app->touch->touch->events.cancel, &event);
#endif
}

}
