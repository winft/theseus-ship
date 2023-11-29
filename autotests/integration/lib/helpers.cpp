/*
    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "helpers.h"

#include "setup.h"

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

namespace KWin::detail::test
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

input::wayland::cursor<space::input_t>* cursor()
{
    return app()->base->mod.space->input->cursor.get();
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
    auto const& outputs = app()->base->outputs;
    assert(index < outputs.size());
    return outputs.at(index);
}

void set_current_output(int index)
{
    auto const& outputs = app()->base->outputs;
    auto output = base::get_output(outputs, index);
    QVERIFY(output);
    base::set_current_output(*app()->base, output);
}

void test_outputs_default()
{
    test_outputs_geometries({QRect(0, 0, 1280, 1024), QRect(1280, 0, 1280, 1024)});
}

void test_outputs_geometries(std::vector<QRect> const& geometries)
{
    auto const& outputs = app()->base->outputs;
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

wayland_window* render_and_wait_for_shown(std::unique_ptr<Clt::Surface> const& surface,
                                          QSize const& size,
                                          QColor const& color,
                                          QImage::Format const& format,
                                          int timeout)
{
    return render_and_wait_for_shown(get_client(), surface, size, color, format, timeout);
}

wayland_window* render_and_wait_for_shown(client const& clt,
                                          std::unique_ptr<Clt::Surface> const& surface,
                                          QSize const& size,
                                          QColor const& color,
                                          QImage::Format const& format,
                                          int timeout)
{
    QSignalSpy clientAddedSpy(app()->base->mod.space->qobject.get(),
                              &space::qobject_t::wayland_window_added);
    if (!clientAddedSpy.isValid()) {
        return nullptr;
    }
    render(clt, surface, size, color, format);
    flush_wayland_connection(clt);
    if (!clientAddedSpy.wait(timeout)) {
        return nullptr;
    }

    auto win_id = clientAddedSpy.first().first().value<quint32>();
    return std::get<wayland_window*>(app()->base->mod.space->windows_map.at(win_id));
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
                       Clt::xdg_shell_positioner_data positioner_data,
                       CreationSetup creationSetup)
{
    return create_xdg_shell_popup(
        get_client(), surface, parent_toplevel, std::move(positioner_data), creationSetup);
}

std::unique_ptr<Clt::XdgShellPopup>
create_xdg_shell_popup(client const& clt,
                       std::unique_ptr<Clt::Surface> const& surface,
                       std::unique_ptr<Clt::XdgShellToplevel> const& parent_toplevel,
                       Clt::xdg_shell_positioner_data positioner_data,
                       CreationSetup creationSetup)
{
    if (!clt.interfaces.xdg_shell) {
        return nullptr;
    }

    std::unique_ptr<Clt::XdgShellPopup> popup;
    if (parent_toplevel) {
        popup.reset(clt.interfaces.xdg_shell->create_popup(
            surface.get(), parent_toplevel.get(), positioner_data));
    } else {
        popup.reset(clt.interfaces.xdg_shell->create_popup(surface.get(), positioner_data));
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
    QSignalSpy configureRequestedSpy(shell_toplevel.get(), &Clt::XdgShellToplevel::configured);
    QVERIFY(configureRequestedSpy.isValid());
    surface->commit(Clt::Surface::CommitFlag::None);
    QVERIFY(configureRequestedSpy.wait());
    shell_toplevel->ackConfigure(configureRequestedSpy.back().front().toInt());
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

void lock_screen()
{
    QVERIFY(!base::wayland::is_screen_locked(app()->base));

    QSignalSpy lockStateChangedSpy(ScreenLocker::KSldApp::self(),
                                   &ScreenLocker::KSldApp::lockStateChanged);
    QVERIFY(lockStateChangedSpy.isValid());
    QSignalSpy lockWatcherSpy(app()->base->mod.space->desktop->screen_locker_watcher.get(),
                              &desktop::screen_locker_watcher::locked);
    QVERIFY(lockWatcherSpy.isValid());

    ScreenLocker::KSldApp::self()->lock(ScreenLocker::EstablishLock::Immediate);
    QCOMPARE(lockStateChangedSpy.count(), 1);

    QVERIFY(base::wayland::is_screen_locked(app()->base));
    QVERIFY(lockWatcherSpy.wait());
    QCOMPARE(lockWatcherSpy.count(), 1);
    QCOMPARE(lockStateChangedSpy.count(), 2);

    QVERIFY(app()->base->mod.space->desktop->screen_locker_watcher->is_locked());
}

void unlock_screen()
{
    QSignalSpy lockStateChangedSpy(ScreenLocker::KSldApp::self(),
                                   &ScreenLocker::KSldApp::lockStateChanged);
    QVERIFY(lockStateChangedSpy.isValid());
    QSignalSpy lockWatcherSpy(app()->base->mod.space->desktop->screen_locker_watcher.get(),
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

    QVERIFY(!base::wayland::is_screen_locked(app()->base));

    QVERIFY(!app()->base->mod.space->desktop->screen_locker_watcher->is_locked());
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
    return "wayland-kwinft-test-" + base + "-0";
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
    auto test_app = app();

    QVERIFY(test_app->pointer);
    wlr_pointer_motion_absolute_event event{};
    event.pointer = test_app->pointer;

    event.time_msec = time;

    auto const screens_size = test_app->base->topology.size;
    event.x = position.x() / screens_size.width();
    event.y = position.y() / screens_size.height();

    wlr_signal_emit_safe(&test_app->pointer->events.motion_absolute, &event);
    wlr_signal_emit_safe(&test_app->pointer->events.frame, test_app->pointer);
}

void pointer_button_impl(uint32_t button, uint32_t time, wlr_button_state state)
{
    auto test_app = app();

    QVERIFY(test_app->pointer);
    wlr_pointer_button_event event{};
    event.pointer = test_app->pointer;

    event.time_msec = time;

    event.button = button;
    event.state = state;

    wlr_signal_emit_safe(&test_app->pointer->events.button, &event);
    wlr_signal_emit_safe(&test_app->pointer->events.frame, test_app->pointer);
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
    auto test_app = app();

    QVERIFY(test_app->pointer);
    wlr_pointer_axis_event event{};
    event.pointer = test_app->pointer;

    event.time_msec = time;

    event.delta = delta;
    event.delta_discrete = discrete_delta;
    event.orientation = orientation;
    event.source = source;

    wlr_signal_emit_safe(&test_app->pointer->events.axis, &event);
    wlr_signal_emit_safe(&test_app->pointer->events.frame, test_app->pointer);
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

void keyboard_key_pressed(uint32_t key, uint32_t time)
{
    keyboard_key_impl(key, time, true, WL_KEYBOARD_KEY_STATE_PRESSED, app()->keyboard);
}

void keyboard_key_released(uint32_t key, uint32_t time)
{
    keyboard_key_impl(key, time, true, WL_KEYBOARD_KEY_STATE_RELEASED, app()->keyboard);
}

void keyboard_key_pressed(uint32_t key, uint32_t time, wlr_keyboard* keyboard)
{
    keyboard_key_impl(key, time, true, WL_KEYBOARD_KEY_STATE_PRESSED, keyboard);
}

KWIN_EXPORT void keyboard_key_released(uint32_t key, uint32_t time, wlr_keyboard* keyboard)
{
    keyboard_key_impl(key, time, true, WL_KEYBOARD_KEY_STATE_RELEASED, keyboard);
}

QPointF get_relative_touch_position(QPointF const& pos)
{
    auto output = base::get_nearest_output(app()->base->outputs, pos.toPoint());
    assert(output);

    auto output_size = output->geometry().size();
    return QPointF(pos.x() / output_size.width(), pos.y() / output_size.height());
}

void touch_down(int32_t id, QPointF const& position, uint32_t time)
{
    auto test_app = app();
    QVERIFY(test_app->touch);

    wlr_touch_down_event event{};
    event.touch = test_app->touch;

    event.time_msec = time;

    event.touch_id = id;

    auto rel_pos = get_relative_touch_position(position);
    event.x = rel_pos.x();
    event.y = rel_pos.y();

    wlr_signal_emit_safe(&test_app->touch->events.down, &event);
}

void touch_up(int32_t id, uint32_t time)
{
    auto test_app = app();
    QVERIFY(test_app->touch);

    wlr_touch_up_event event{};
    event.touch = test_app->touch;

    event.time_msec = time;

    event.touch_id = id;

    wlr_signal_emit_safe(&test_app->touch->events.up, &event);
}

void touch_motion(int32_t id, QPointF const& position, uint32_t time)
{
    auto test_app = app();
    QVERIFY(test_app->touch);

    wlr_touch_motion_event event{};
    event.touch = test_app->touch;

    event.time_msec = time;

    event.touch_id = id;

    auto rel_pos = get_relative_touch_position(position);
    event.x = rel_pos.x();
    event.y = rel_pos.y();

    wlr_signal_emit_safe(&test_app->touch->events.motion, &event);
}

void touch_cancel()
{
    auto test_app = app();
    QVERIFY(test_app->touch);

    wlr_touch_cancel_event event{};
    event.touch = test_app->touch;

    wlr_signal_emit_safe(&test_app->touch->events.cancel, &event);
}

void swipe_begin(uint32_t fingers, uint32_t time)
{
    auto test_app = app();
    QVERIFY(test_app->pointer);

    wlr_pointer_swipe_begin_event event{
        .pointer = test_app->pointer, .time_msec = time, .fingers = fingers};

    wlr_signal_emit_safe(&test_app->pointer->events.swipe_begin, &event);
}

void swipe_update(uint32_t fingers, double dx, double dy, uint32_t time)
{
    auto test_app = app();
    QVERIFY(test_app->pointer);

    wlr_pointer_swipe_update_event event{
        .pointer = test_app->pointer, .time_msec = time, .fingers = fingers, .dx = dx, .dy = dy};

    wlr_signal_emit_safe(&test_app->pointer->events.swipe_update, &event);
}

void swipe_end(uint32_t time)
{
    auto test_app = app();
    QVERIFY(test_app->pointer);

    wlr_pointer_swipe_end_event event{
        .pointer = test_app->pointer, .time_msec = time, .cancelled = false};

    wlr_signal_emit_safe(&test_app->pointer->events.swipe_end, &event);
}

void swipe_cancel(uint32_t time)
{
    auto test_app = app();
    QVERIFY(test_app->pointer);

    wlr_pointer_swipe_end_event event{
        .pointer = test_app->pointer, .time_msec = time, .cancelled = true};

    wlr_signal_emit_safe(&test_app->pointer->events.swipe_end, &event);
}

void pinch_begin(uint32_t fingers, uint32_t time)
{
    auto test_app = app();
    QVERIFY(test_app->pointer);

    wlr_pointer_pinch_begin_event event{
        .pointer = test_app->pointer, .time_msec = time, .fingers = fingers};

    wlr_signal_emit_safe(&test_app->pointer->events.pinch_begin, &event);
}

void pinch_update(uint32_t fingers,
                  double dx,
                  double dy,
                  double scale,
                  double rotation,
                  uint32_t time)
{
    auto test_app = app();
    QVERIFY(test_app->pointer);

    wlr_pointer_pinch_update_event event{.pointer = test_app->pointer,
                                         .time_msec = time,
                                         .fingers = fingers,
                                         .dx = dx,
                                         .dy = dy,
                                         .scale = scale,
                                         .rotation = rotation};

    wlr_signal_emit_safe(&test_app->pointer->events.pinch_update, &event);
}

void pinch_end(uint32_t time)
{
    auto test_app = app();
    QVERIFY(test_app->pointer);

    wlr_pointer_pinch_end_event event{
        .pointer = test_app->pointer, .time_msec = time, .cancelled = false};

    wlr_signal_emit_safe(&test_app->pointer->events.pinch_end, &event);
}

void pinch_cancel(uint32_t time)
{
    auto test_app = app();
    QVERIFY(test_app->pointer);

    wlr_pointer_pinch_end_event event{
        .pointer = test_app->pointer, .time_msec = time, .cancelled = true};

    wlr_signal_emit_safe(&test_app->pointer->events.pinch_end, &event);
}

void hold_begin(uint32_t fingers, uint32_t time)
{
    auto test_app = app();
    QVERIFY(test_app->pointer);

    wlr_pointer_hold_begin_event event{
        .pointer = test_app->pointer, .time_msec = time, .fingers = fingers};

    wlr_signal_emit_safe(&test_app->pointer->events.hold_begin, &event);
}

void hold_end(uint32_t time)
{
    auto test_app = app();
    QVERIFY(test_app->pointer);

    wlr_pointer_hold_end_event event{
        .pointer = test_app->pointer, .time_msec = time, .cancelled = false};

    wlr_signal_emit_safe(&test_app->pointer->events.hold_end, &event);
}

void hold_cancel(uint32_t time)
{
    auto test_app = app();
    QVERIFY(test_app->pointer);

    wlr_pointer_hold_end_event event{
        .pointer = test_app->pointer, .time_msec = time, .cancelled = true};

    wlr_signal_emit_safe(&test_app->pointer->events.hold_end, &event);
}

}
